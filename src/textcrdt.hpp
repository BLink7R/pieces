#pragma once

#include <map>
#include <tuple>
#include <unordered_set>

#include "piecetree.hpp"
#include "rangetree.hpp"
#include "taggedptr.hpp"

template <typename FormatProvider = void, typename CharT = char>
class PieceCRDT
{
	struct Empty
	{
	};
	using Format = std::conditional_t<std::is_same_v<FormatProvider, void>, Empty, FormatProvider>;

private:
	uint32_t lamport_stamp;
	const ReplicaID local_id;
	const ReplicaID origin_id;

protected:
	OrderedSet<Replica, 4> replicas;
	mutable PieceTree<4, CharT> piece_tree;
	RangeTree<4> deletions;
	Format format_provider;

public:
	using Iterator = typename PieceTree<4, CharT>::Iterator;
	using String = std::basic_string<CharT>;

	PieceCRDT(const ReplicaID &origin = {})
		: lamport_stamp(0),
		  local_id(generateReplicaID()),
		  origin_id(origin.is_nil() ? local_id : origin),
		  piece_tree(storeOp<Segment<CharT>>(ReplicaID(), 1, String(1, CharT(0)))) // EOF
	{
	}
	PieceCRDT(const PieceCRDT &) = delete;
	PieceCRDT &operator=(const PieceCRDT &) = delete;
	PieceCRDT(PieceCRDT &&) = default;
	PieceCRDT &operator=(PieceCRDT &&) = delete;

	~PieceCRDT() = default;

	// TODO: remove this function
	static PieceCRDT fork(const PieceCRDT &other)
	{
		PieceCRDT new_crdt(other.origin());
		new_crdt.apply(other.diff({}));
		return new_crdt;
	}

	const ReplicaID id() const
	{
		return local_id;
	}

	const ReplicaID origin() const
	{
		return origin_id;
	}

	uint32_t stamp() const
	{
		return lamport_stamp;
	}

	auto begin() const
	{
		return piece_tree.begin();
	}

	auto end() const
	{
		return piece_tree.end();
	}

	Iterator find(size_t offset) const
	{
		return piece_tree.upper_bound(offset);
	}

	auto size() const
	{
		return (--piece_tree.end()).position().visible;
	}

	String toString() const
	{
		String res;
		res.reserve(size());
		for (auto it = piece_tree.begin(), end_it = --piece_tree.end(); it != end_it; ++it)
		{
			// std::cout << std::string(it->data, it->len) << " " << it->isRemoved() << "\n";
			if (it->isRemoved())
				continue;
			res.append(it->data, it->len);
		}
		return res;
	}

	Anchor anchor(size_t offset) const
	{
		Anchor anchor;
		if (offset == 0)
			return anchor;
		auto it = piece_tree.lower_bound(offset);
		if (it.isNull())
			return anchor;
		StoredContent *seg = it->seg;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.offset = static_cast<int32_t>(offset - it.position().visible + it->seg_offset);
		return anchor;
	}

	Anchor reversedAnchor(size_t offset) const
	{
		Anchor anchor;
		auto it = piece_tree.upper_bound(offset);
		if (it.isNull())
			return anchor;
		StoredContent *seg = it->seg;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.offset = static_cast<int32_t>(static_cast<int64_t>(offset) - it.position().visible + it->seg_offset - seg->size);
		return anchor;
	}

	// implement the [Fugue](https://arxiv.org/abs/2305.00583) algorithm to solve the interleaving problem.
	// brief of this algorithm: always attach the insertion to the newer piece at left/right.
	Anchor insertAnchor(size_t offset) const
	{
		auto it = piece_tree.upper_bound(offset);
		Anchor anchor;
		if (offset > 0 && offset == it.position().visible)
		{ // as begining of the piece
			StoredContent *seg_right = it->seg;
			auto it_before = it;
			--it_before;
			StoredContent *seg_left = it_before->seg;
			if (seg_left == seg_right || *seg_left < *seg_right)
			{ // right is newer
				anchor.replica = seg_right->replica->id;
				anchor.stamp = seg_right->stamp;
				anchor.offset = static_cast<int32_t>(it->seg_offset) - seg_right->size; // reversed anchor
			}
			else
			{ // left is newer
				anchor.replica = seg_left->replica->id;
				anchor.stamp = seg_left->stamp;
				anchor.offset = static_cast<int32_t>(it_before->seg_offset + it_before->len); // normal anchor
			}
		}
		else
		{ // when inside the piece, we prefer to insert before the character
			StoredContent *seg = it->seg;
			anchor.replica = seg->replica->id;
			anchor.stamp = seg->stamp;
			anchor.offset = static_cast<int32_t>(static_cast<int64_t>(offset) - it.position().visible + it->seg_offset - seg->size); // reversed anchor
		}
		return anchor;
	}

	size_t offset(const Anchor &anchor) const
	{
		StoredAnchor stored = toStored(anchor);
		if (stored.seg == nullptr)
			return String::npos;
		auto it = piece_tree.find(stored);
		if (it->isRemoved())
			return it.position().visible;
		// it.position().visible is the start of the piece, add the offset within the piece
		return it.position().visible + (stored.segOffset() - static_cast<int32_t>(it->seg_offset));
	}

	std::vector<OperationID> frontline() const
	{
		std::vector<OperationID> res;
		for (const auto &replica : replicas)
		{
			if (!replica.id.is_nil() && !replica.operations.empty())
				res.push_back({replica.id, static_cast<uint32_t>(replica.operations.size() - 1)});
		}
		return res;
	}

	std::vector<std::unique_ptr<Operation>> diff(const std::vector<OperationID> &frontline) const
	{
		std::vector<std::unique_ptr<Operation>> res;
		for (const auto &replica : replicas)
		{
			uint32_t start_stamp = 2; // start from 2, as 1 is initial EOF segment
			for (const auto &opID : frontline)
			{
				if (opID.replica == replica.id)
				{
					start_stamp = std::max(start_stamp, opID.stamp + 1);
					break;
				}
			}

			for (uint32_t i = start_stamp; i < replica.operations.size(); ++i)
			{
				const auto *stored = replica.operations[i].get();
				if (!stored)
					continue;

				switch (stored->type())
				{
				case OperationType::Insert:
				{
					const auto *content = static_cast<const StoredContent *>(stored);
					assert(content->anchor.seg != nullptr);
					const auto *parent = content->anchor.seg;
					Anchor anchor(parent->operationID(), content->anchor.offset);
					if (content->isObject())
						res.push_back(std::make_unique<Insertion<CharT>>(replica.id, i, anchor, String(), static_cast<const StoredObject<CharT> *>(content)->id));
					else
					{
						const auto *text_seg = static_cast<const Segment<CharT> *>(content);
						res.push_back(std::make_unique<Insertion<CharT>>(replica.id, i, anchor, String(text_seg->data.get())));
					}
					break;
				}
				case OperationType::Delete:
				{
					const auto *del = static_cast<const StoredDeletion *>(stored);
					StoredAnchor left_anchor = del->left->anchor;
					StoredAnchor right_anchor = del->right->anchor;
					res.push_back(std::make_unique<Deletion>(replica.id, i, left_anchor.toAnchor(), right_anchor.toAnchor()));
					break;
				}
				case OperationType::Undo:
				{
					const auto *undo = static_cast<const StoredUndo *>(stored);
					res.push_back(std::make_unique<UndoOperation>(replica.id, i, undo->target->operationID()));
					break;
				}
				case OperationType::Redo:
				{
					const auto *redo = static_cast<const StoredRedo *>(stored);
					res.push_back(std::make_unique<RedoOperation>(replica.id, i, redo->target->operationID()));
					break;
				}
				default:
					break;
				}
			}
		}
		return res;
	}

	void apply(const std::vector<std::unique_ptr<Operation>> &ops)
	{
		for (const auto &op : ops)
		{
			apply(*op);
		}
	}

	bool apply(const Operation &op)
	{
		switch (op.type)
		{
		case OperationType::Insert:
			return insert(static_cast<const Insertion<CharT> &>(op));
		case OperationType::Delete:
			return del(static_cast<const Deletion &>(op));
		case OperationType::Undo:
			return undo(static_cast<const UndoOperation &>(op));
		case OperationType::Redo:
			return redo(static_cast<const RedoOperation &>(op));
		case OperationType::RangeFormat:
			if constexpr (!std::is_same_v<FormatProvider, void>)
				return format_provider.apply(this, op);
			else
				return false;
			break;
		default:
			return false;
		}
	}

	bool insert(const Insertion<CharT> &op)
	{
		if (op.str.empty() && op.object_id.empty())
			return false; // no-op
		auto anchor = toStored(op.anchor);
		if (anchor.seg == nullptr)
			return false; // invalid anchor

		StoredContent *segment = op.object_id.empty()
									 ? static_cast<StoredContent *>(storeOp<Segment<CharT>>(op.replica, op.stamp, op.str))
									 : static_cast<StoredContent *>(storeOp<StoredObject<CharT>>(op.replica, op.stamp, op.object_id));
		if (segment == nullptr)
			return false; // duplicate operation

		segment->anchor = anchor;
		piece_tree.insert(segment);
		return true;
	}

	auto &formatProvider()
		requires(!std::is_same_v<FormatProvider, void>)
	{
		return format_provider;
	}

	template <typename T>
	T style(Iterator it, std::string style_name) const
		requires(!std::is_same_v<FormatProvider, void>)
	{
		int style_key = format_provider.styleKey(style_name);
		if (style_key < 0)
			return T{};
		StoredRangeOp *op = it->styles[style_key];
		if (op == nullptr)
			return format_provider.template getDefaultValue<T>(style_name);
		return static_cast<StoredFormat<T> *>(op)->value;
	}

	template <typename RangeType, typename T>
	bool format(const Formatting<RangeType, T> &op)
		requires(!std::is_same_v<FormatProvider, void>)
	{
		int style_key = format_provider.styleKey(op.key);
		if (style_key < 0)
			return false; // invalid style
		if (op.range.begin == op.range.end)
			return false; // no-op
		auto begin = toStored(op.range.begin);
		auto end = toStored(op.range.end);
		if (begin.seg == nullptr || end.seg == nullptr)
			return false; // invalid anchor

		auto *stored_op = storeOp<StoredFormat<T>>(op.replica, op.stamp, style_key, op.value);
		if (!stored_op)
			return false; // duplicate operation

		auto style_tree = format_provider.style(stored_op->key);
		auto [left_it, right_it] = style_tree.apply(
			RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
		stored_op->left = &*left_it;
		stored_op->right = &*right_it;

		redoFormat(stored_op);
		return true;
	}

	bool del(const Deletion &op)
	{
		if (op.range.begin == op.range.end)
			return false; // no-op
		auto begin = toStored(op.range.begin);
		auto end = toStored(op.range.end);
		if (begin.seg == nullptr || end.seg == nullptr)
			return false; // invalid anchor

		auto *stored_op = storeOp<StoredDeletion>(op.replica, op.stamp);
		if (!stored_op)
			return false; // duplicate operation

		auto [left_it, right_it] = deletions.apply(
			RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
		stored_op->left = &*left_it;
		stored_op->right = &*right_it;

		redoDel(stored_op);
		return true;
	}

	// TODO: op is received from other replicas, do we need to transform it?
	// we need to ensure not undo/redo an undo/redo operation before send it to other replicas
	bool undo(const UndoOperation &op)
	{
		auto replica_it = replicas.find(op.target.replica);
		if (replica_it == replicas.end())
			return false;
		if (replica_it->operations.size() <= op.target.stamp)
			return false;
		StoredOperation *target = replica_it->operations[op.target.stamp].get();
		if (target->type() == OperationType::Undo)
		{
			target = static_cast<StoredUndo *>(target)->target;
			return redo(RedoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type() == OperationType::Redo)
		{
			target = static_cast<StoredRedo *>(target)->target;
		}
		auto *undo_op = storeOp<StoredUndo>(op.replica, op.stamp, static_cast<UndoRedoableOp *>(target));
		if (!undo_op)
			return false;
		undoOp(undo_op);
		return true;
	}

	bool redo(const RedoOperation &op)
	{
		auto replica_it = replicas.find(op.target.replica);
		if (replica_it == replicas.end())
			return false;
		if (replica_it->operations.size() <= op.target.stamp)
			return false;
		StoredOperation *target = replica_it->operations[op.target.stamp].get();
		if (target->type() == OperationType::Undo)
		{
			target = static_cast<StoredUndo *>(target)->target;
			return undo(UndoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type() == OperationType::Redo)
		{
			target = static_cast<StoredRedo *>(target)->target;
		}
		auto *redo_op = storeOp<StoredRedo>(op.replica, op.stamp, static_cast<UndoRedoableOp *>(target));
		if (!redo_op)
			return false;
		redoOp(redo_op);
		return true;
	}

private:
	void redoOp(StoredRedo *op)
	{
		UndoRedoableOp *target = op->target;
		if (target->undoredo && *op < *target->undoredo)
			return; // LWW - last write wins
		switch (target->type())
		{
		case OperationType::Insert:
			redoInsertion(static_cast<StoredContent *>(target));
			break;
		case OperationType::Delete:
			redoDel(static_cast<StoredDeletion *>(target));
			break;
		case OperationType::RangeFormat:
			if constexpr (!std::is_same_v<FormatProvider, void>)
				redoFormat(static_cast<StoredRangeOp *>(target));
			else
				assert(false && "format operation is not supported");
			break;
		case OperationType::Undo:
		case OperationType::Redo:
			assert(false && "cannot redo an undo/redo operation directly");
			break;
		default:
			break;
		}
		target->undoredo = op;
	}

	void undoOp(StoredUndo *op)
	{
		UndoRedoableOp *target = op->target;
		if (target->undoredo && *op < *target->undoredo)
			return; // LWW - last write wins
		switch (target->type())
		{
		case OperationType::Insert:
			undoInsertion(static_cast<StoredContent *>(target));
			break;
		case OperationType::Delete:
			undoDel(static_cast<StoredDeletion *>(target));
			break;
		case OperationType::RangeFormat:
			if constexpr (!std::is_same_v<FormatProvider, void>)
				undoFormat(static_cast<StoredRangeOp *>(target));
			else
				assert(false && "format operation is not supported");
			break;
		case OperationType::Undo:
		case OperationType::Redo:
			assert(false && "cannot undo an undo/redo operation directly");
			break;
		default:
			break;
		}
		target->undoredo = op;
	}

	void redoDel(StoredDeletion *target)
	{
		assert(target->left->status == TagStatus::Undone && target->right->status == TagStatus::Undone);
		auto left_piece = piece_tree.find(target->left->anchor);
		auto right_piece = piece_tree.find(target->right->anchor);

		// Update tag->old for left and right boundary pieces by checking first and last pieces
		// inside the deletion. We do not check pieces outside the deletion range because it
		// needs to process the right closed anchor case.
		{
			auto piece_before = left_piece;
			target->left->old.setBad();
			auto op = piece_before->tombStone;
			assert(op == nullptr || op->right->old.isGood());
			if (op == nullptr)
				target->left->old = nullptr;
			else if (op->left->anchor != target->left->anchor)
			{
				if (*op < *target)
					target->left->old = op;
			}
			else if (op->left->old == nullptr || *op->left->old < *target)
			{
				assert(op->left->status == TagStatus::Active && "tombStone should be Active");
				target->left->old = op->left->old;
			}
		}
		{
			auto piece_after = right_piece;
			target->right->old.setBad();
			auto op = piece_after->tombStone;
			assert(op == nullptr || op->left->old.isGood());
			if (op == nullptr)
				target->right->old = nullptr;
			else if (op->right->anchor != target->right->anchor)
			{
				if (*op < *target)
					target->right->old = op;
			}
			else if (op->right->old == nullptr || *op->right->old < *target)
			{
				assert(op->right->status == TagStatus::Active && "tombStone should be Active");
				target->right->old = op->right->old;
			}
		}

		redoRangeOp(target, [](Piece<CharT> *piece, StoredRangeOp *op)
		{
			if (piece->tombStone == nullptr || *piece->tombStone < *op)
				piece->tombStone = static_cast<StoredRangeOp *>(op);
		});
		piece_tree.update(left_piece, right_piece);
	}

	void undoDel(StoredDeletion *target)
	{
		auto ops_covered = undoRangeOp(target, [target](Piece<CharT> *piece, StoredRangeOp *newest)
		{
			if (piece->tombStone == target)
				piece->tombStone = static_cast<StoredRangeOp *>(newest);
		});

		// the `old` tag is already updated in `undoRangeOp`, so directly call `redoRangeOp`
		for (auto ops : ops_covered)
		{
			redoRangeOp(ops, [](Piece<CharT> *piece, StoredRangeOp *op)
			{
				if (piece->tombStone == nullptr || *piece->tombStone < *op)
					piece->tombStone = static_cast<StoredRangeOp *>(op);
			});
		}

		auto left_piece = piece_tree.find(target->left->anchor);
		auto right_piece = piece_tree.find(target->right->anchor);
		piece_tree.update(left_piece, right_piece);
	}

	void redoFormat(StoredRangeOp *target)
		requires(!std::is_same_v<FormatProvider, void>)
	{
		assert(target->left->status == TagStatus::Undone && target->right->status == TagStatus::Undone);
		int style_key = target->styleType();
		auto left_piece = piece_tree.find(target->left->anchor);
		auto right_piece = piece_tree.find(target->right->anchor);

		// Update tag->old for left and right boundary pieces by checking first and last pieces
		// inside the deletion. We do not check pieces outside the deletion range because it
		// needs to process the right closed anchor case.
		{
			auto piece_before = left_piece;
			target->left->old.setBad();
			auto op = piece_before->styles[style_key];
			assert(op == nullptr || op->right->old.isGood());
			if (op == nullptr)
				target->left->old = nullptr;
			else if (op->left->anchor != target->left->anchor)
			{
				if (*op < *target)
					target->left->old = op;
			}
			else if (op->left->old == nullptr || *op->left->old < *target)
			{
				assert(op->left->status == TagStatus::Active && "tombStone should be Active");
				target->left->old = op->left->old;
			}
		}
		{
			auto piece_after = right_piece;
			target->right->old.setBad();
			auto op = piece_after->styles[style_key];
			assert(op == nullptr || op->left->old.isGood());
			if (op == nullptr)
				target->right->old = nullptr;
			else if (op->right->anchor != target->right->anchor)
			{
				if (*op < *target)
					target->right->old = op;
			}
			else if (op->right->old == nullptr || *op->right->old < *target)
			{
				assert(op->right->status == TagStatus::Active && "tombStone should be Active");
				target->right->old = op->right->old;
			}
		}

		std::multimap<void *, Piece<CharT> *> cache;
		redoRangeOp(target, [style_key, &cache](Piece<CharT> *piece, StoredRangeOp *op)
		{
			if (piece->styles[style_key] == nullptr || *piece->styles[style_key] < *op)
				cache.insert({piece->styles.raw(), piece});
		});
		for (auto it = cache.begin(); it != cache.end();)
		{
			auto range = cache.equal_range(it->first);
			Piece<CharT> *first_piece = range.first->second;
			Formats new_formats = first_piece->styles;
			new_formats.set(style_key, target);
			for (auto jt = range.first; jt != range.second; ++jt)
			{
				jt->second->styles = new_formats;
			}
			it = range.second;
		}
	}

	void undoFormat(StoredRangeOp *target)
		requires(!std::is_same_v<FormatProvider, void>)
	{
		int style_key = target->styleType();

		std::multimap<std::pair<void *, StoredRangeOp *>, Piece<CharT> *> cache;
		auto ops_covered = undoRangeOp(target, [target, style_key, &cache](Piece<CharT> *piece, StoredRangeOp *newest)
		{
			if (piece->styles[style_key] == target)
				cache.insert({{piece->styles.raw(), newest}, piece});
		});
		for (auto it = cache.begin(); it != cache.end();)
		{
			auto range = cache.equal_range(it->first);
			StoredRangeOp *newest_op = it->first.second;
			Piece<CharT> *first_piece = range.first->second;
			Formats new_formats = first_piece->styles;
			new_formats.set(style_key, newest_op);
			for (auto jt = range.first; jt != range.second; ++jt)
			{
				Piece<CharT> *piece = jt->second;
				piece->styles = new_formats;
			}
			it = range.second;
		}

		for (auto op : ops_covered)
		{
			std::multimap<void *, Piece<CharT> *> cache;
			redoRangeOp(op, [style_key, &cache](Piece<CharT> *piece, StoredRangeOp *op)
			{
				if (piece->styles[style_key] == nullptr || *piece->styles[style_key] < *op)
					cache.insert({piece->styles.raw(), piece});
			});
			for (auto it = cache.begin(); it != cache.end();)
			{
				auto range = cache.equal_range(it->first);
				Piece<CharT> *first_piece = range.first->second;
				Formats new_formats = first_piece->styles;
				new_formats.set(style_key, op);
				for (auto jt = range.first; jt != range.second; ++jt)
				{
					jt->second->styles = new_formats;
				}
				it = range.second;
			}
		}
	}

	void redoInsertion(StoredContent *target)
	{
		if (target->undo_del != nullptr)
			undoDel(target->undo_del.get());
	}

	void undoInsertion(StoredContent *target)
	{
		if (target->undo_del == nullptr)
		{
			auto stored_op = new StoredDeletion();
			stored_op->replica = target->replica;
			stored_op->stamp = target->stamp;

			auto begin = StoredAnchor(target, -target->size);
			auto end = StoredAnchor(target, target->size);
			auto [left_it, right_it] = deletions.apply(
				RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
			stored_op->left = &*left_it;
			stored_op->right = &*right_it;

			target->undo_del.reset(stored_op);
		}
		redoDel(target->undo_del.get());
	}

	template <typename UpdateFunc>
	void redoRangeOp(StoredRangeOp *stored_op, const UpdateFunc &updateFunc)
	{
		auto left_it = typename RangeTree<4>::Iterator(stored_op->left);
		auto right_it = typename RangeTree<4>::Iterator(stored_op->right);

		auto begin_piece = piece_tree.find(stored_op->left->anchor);
		auto end_piece = piece_tree.find(stored_op->right->anchor);
		if (!stored_op->right->anchor.isReversed())
		{ // for right closed anchor
			++end_piece;
		}
		for (; begin_piece != end_piece; ++begin_piece)
		{
			updateFunc(&*begin_piece, stored_op);
		}

		bool has_across = false;
		auto first_across = left_it;
		auto last_across = right_it;
		// find and update all acrossing tags
		auto it = left_it;
		for (++it; it != right_it; ++it)
		{
			RangeTag *tag = &*it;
			if (tag->status == TagStatus::Undone || tag->status == TagStatus::UnUsed)
				continue;
			if ((tag->old == nullptr || *tag->old < *stored_op) && (*stored_op < *tag->cur))
			{
				has_across = true;
				if (first_across == left_it)
					first_across = it;
				if (last_across != right_it && last_across != first_across)
					last_across->old = stored_op;
				last_across = it;
			}
		}

		// update left and right tags
		if (!has_across)
		{
			// case 1: newest operation
			if (left_it->old.isGood() && right_it->old.isGood())
				left_it->status = right_it->status = TagStatus::Active;
			// case 2: fully covered by other operations
			else
			{
				// assert below can be false when it has a common begin/end with other ops
				// assert(left_it->old.isBad() && right_it->old.isBad());
				// TODO: we can apply it instead of marking UnUsed
				left_it->status = right_it->status = TagStatus::UnUsed;
			}
			return;
		}
		// case 3: update the `old` pointers of left and right tags
		left_it->status = right_it->status = TagStatus::Active;
		if (left_it->old.isBad())
		{
			StoredRangeOp *newest = first_across->old;
			auto it = first_across;
			for (--it; it != left_it; --it)
			{
				RangeTag *tag = &*it;
				if (tag->status == TagStatus::Undone || tag->status == TagStatus::UnUsed)
					continue;
				if (tag->is_left && tag->cur == newest)
					newest = tag->old;
				else if (!tag->is_left && (newest == nullptr || *newest < *tag->cur) && (*tag->cur < *stored_op))
				{
					assert(tag->old == newest);
					newest = tag->cur;
				}
			}
			left_it->old = newest;
		}

		if (right_it->old.isBad())
		{
			StoredRangeOp *newest = last_across->old;
			auto it = last_across;
			for (++it; it != right_it; ++it)
			{
				RangeTag *tag = &*it;
				if (tag->status == TagStatus::Undone || tag->status == TagStatus::UnUsed)
					continue;
				if (!tag->is_left && tag->cur == newest)
					newest = tag->old;
				else if (tag->is_left && (*tag->cur < *stored_op) && (newest == nullptr || *newest < *tag->cur))
				{
					assert(tag->old == newest);
					newest = tag->cur;
				}
			}
			right_it->old = newest;
		}
		first_across->old = last_across->old = stored_op;
		assert(left_it->old.isGood() == right_it->old.isGood());
	}

	template <typename UpdateFunc>
	std::vector<StoredRangeOp *> undoRangeOp(StoredRangeOp *stored_op, const UpdateFunc &updateFunc)
	{
		auto left_it = RangeTree<4>::Iterator(stored_op->left);
		auto right_it = RangeTree<4>::Iterator(stored_op->right);

		if (left_it->status == TagStatus::UnUsed || right_it->status == TagStatus::UnUsed)
		{
			left_it->status = right_it->status = TagStatus::Undone;
			return {};
		}
		left_it->status = right_it->status = TagStatus::Undone;

		// find all unused tags to update later
		// unused range ops must be fully covered by another op, so we only need to check ops fully covered by this op
		std::unordered_set<StoredRangeOp *> unused_ops;
		std::vector<StoredRangeOp *> ops_covered;
		auto begin_piece = piece_tree.find(stored_op->left->anchor);
		StoredRangeOp *newest = left_it->old;
		auto it = left_it;
		for (++it;; ++it)
		{
			// update piece tree
			if (it->anchor.isReversed())
			{
				for (; begin_piece->seg != it->anchor.seg || begin_piece->seg_offset != it->anchor.segOffset(); ++begin_piece)
				{
					updateFunc(&*begin_piece, newest);
				}
			}
			else
			{
				for (;; ++begin_piece)
				{
					updateFunc(&*begin_piece, newest);
					if (begin_piece->seg == it->anchor.seg && begin_piece->seg_offset + begin_piece->len == it->anchor.offset)
						break;
				}
			}
			if (it == right_it)
				break;
			// update tags
			RangeTag *tag = &*it;
			if (tag->status == TagStatus::Undone)
				continue;
			if (tag->status == TagStatus::UnUsed && *stored_op < *tag->cur)
				continue;
			if (tag->status == TagStatus::Active && tag->old != nullptr && *stored_op < *tag->old)
				continue;
			if (tag->old == stored_op)
			{
				tag->old = newest;
			}
			else if (tag->is_left)
			{
				if (tag->status == TagStatus::UnUsed)
				{
					unused_ops.insert(tag->cur);
					if (newest == nullptr || *newest < *tag->cur)
						tag->old = newest;
					// an op with left and right tags both older than newest can still be newer than newest in middle
					// so all unused tags should be recorded
					else
						tag->old.setBad();
					continue;
				}
				else if (newest == nullptr || *newest < *tag->cur)
				{
					assert(tag->old == newest);
					newest = tag->cur;
				}
			}
			else if (!tag->is_left)
			{
				if (tag->status == TagStatus::UnUsed)
				{
					if (unused_ops.find(tag->cur) != unused_ops.end())
					{
						ops_covered.push_back(tag->cur);
						if (newest == nullptr || *newest < *tag->cur)
							tag->old = newest;
						else
							tag->old.setBad();
					}
				}
				else if (tag->cur == newest)
					newest = tag->old;
			}
		}

		// covered ops must be done from newest to oldest, as redoing older op can affect the `old` tag
		std::sort(ops_covered.begin(), ops_covered.end(),
				  [](StoredRangeOp *a, StoredRangeOp *b)
		{
			return *b < *a;
		});
		return ops_covered;
	}

	Replica *getReplica(const ReplicaID &id)
	{
		auto it = replicas.find(id, [](const Replica &a, const ReplicaID &b)
		{
			return a.id < b;
		});
		if (it == replicas.end() || it->id != id)
			return &*replicas.insert(Replica{.id = id});
		return &*it;
	}
	StoredAnchor toStored(const Anchor &anchor) const
	{
		auto replica_it = replicas.find(anchor.replica);
		if (replica_it == replicas.end())
			return StoredAnchor();

		auto replica = replica_it;
		if (anchor.stamp >= replica->operations.size())
			return StoredAnchor();

		auto &seg_ptr = replica->operations[anchor.stamp];
		if (!seg_ptr || seg_ptr->type() != OperationType::Insert)
			return StoredAnchor();

		StoredContent *seg = static_cast<StoredContent *>(seg_ptr.get());
		if (anchor.offset == 0 || seg->size < std::abs(anchor.offset))
			return StoredAnchor();

		return StoredAnchor(seg, anchor.offset);
	}

	template <typename T, typename... Args>
		requires std::is_base_of_v<StoredOperation, T>
	T *storeOp(ReplicaID replica_id, uint32_t stamp, Args &&...args)
	{
		return storeOp<T>(getReplica(replica_id), stamp, std::forward<Args>(args)...);
	}

	template <typename T, typename... Args>
		requires std::is_base_of_v<StoredOperation, T>
	T *storeOp(const Replica *replica, uint32_t stamp, Args &&...args)
	{
		if (stamp < replica->maxStamp())
			return nullptr; // duplicate operation
		lamport_stamp = std::max(lamport_stamp, stamp) + 1;

		replica->operations.resize(stamp + 1);
		assert(replica->operations[stamp] == nullptr);
		replica->operations[stamp] = std::make_unique<T>(std::forward<Args>(args)...);

		T *op = static_cast<T *>(replica->operations[stamp].get());
		op->replica = replica;
		op->stamp = stamp;
		return op;
	}
};
