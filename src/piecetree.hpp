#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <random>
#include <tuple>
#include <unordered_set>
#include <utf8cpp/utf8.h>
#include <utility>
#include <vector>

#include "crdt.hpp"
#include "gb+tree.hpp"
#include "taggedptr.hpp"

struct Replica;
struct Segment;
struct Piece;
struct StoredOperation;
struct StoredRangeOp;
struct StoredDeletion;

struct Replica
{
	ReplicaID id{};
	// TODO: not correct, need virtual deconstructor
	// TODO: change to map to save space
	mutable std::vector<std::unique_ptr<StoredOperation>> operations; // created segments

	uint32_t maxStamp() const
	{
		if (operations.empty())
			return 0;
		return operations.size();
	}

	bool operator<(const Replica &other) const
	{
		return id < other.id;
	}
	bool operator<(const ReplicaID &other) const
	{
		return id < other;
	}
};

struct StoredOperation
{
	const Replica *replica{nullptr};
	uint32_t stamp{0};
	OperationType type;
	bool has_undo{false};

	StoredOperation(OperationType type)
		: type(type) {}

	bool operator<(const StoredOperation &other) const
	{
		if (stamp != other.stamp)
			return stamp < other.stamp;
		return replica->id < other.replica->id;
	}

	OperationID operationID() const
	{
		return OperationID{replica->id, stamp};
	}
};

// Text is stored in segments. Whenever text is inserted, a new segment is created,
// and the target segment with the insertion offset is stored, keeping the target unchanged.
struct Segment : public StoredOperation
{
	size_t insert_pos{0};
	Segment *parent{nullptr};
	Piece *last_piece{nullptr};
	Piece *insert_piece{nullptr};
	mutable std::vector<Segment *> split_child; // as segments are usually small, vector is faster
	std::unique_ptr<const char[]> data{nullptr};
	size_t len{0};
	std::unique_ptr<size_t[]> line_breaks{nullptr};
	size_t line_break_count{0};
	std::unique_ptr<StoredDeletion> undo_op{nullptr};

	Segment(const std::string &str)
		: StoredOperation(OperationType::Insert)
	{
		data = std::make_unique<const char[]>(str.size() + 1);
		memcpy(const_cast<char *>(data.get()), str.c_str(), str.size() + 1);
		len = utf8::distance(data.get(), data.get() + str.size());

		// collect newline positions in UTF-8 character offsets
		if (!str.empty())
		{
			std::vector<size_t> breaks;
			const char *it = str.data();
			const char *end = it + str.size();
			size_t char_index = 0;
			while (it < end)
			{
				if (*it == '\r')
				{
					if ((it + 1) < end && *(it + 1) == '\n')
						breaks.push_back(char_index + 1);
					else
						breaks.push_back(char_index);
				}
				else if (*it == '\n')
					breaks.push_back(char_index + 1);
				utf8::next(it, end);
			}
			line_break_count = breaks.size();
			if (line_break_count != 0)
			{
				line_breaks = std::make_unique<size_t[]>(line_break_count);
				for (size_t i = 0; i < line_break_count; ++i)
					line_breaks[i] = breaks[i];
			}
		}
	}
	~Segment() = default;

	size_t findLineBreak(size_t utf8_offset) const
	{
		if (line_break_count == 0)
			return 0;
		const size_t *begin = line_breaks.get();
		const size_t *end = begin + line_break_count;
		const size_t *it = std::lower_bound(begin, end, utf8_offset);
		return static_cast<size_t>(it - begin);
	}

	Segment(Segment &&other) noexcept = default;
	Segment &operator=(Segment &&other) noexcept = default;
	Segment(const Segment &other) = delete;
	Segment &operator=(const Segment &other) = delete;
};

struct StoredAnchor
{
	Segment *seg{nullptr};
	size_t pos{0};

	StoredAnchor(Segment *seg = nullptr, size_t pos = 0)
		: seg(seg), pos(pos) {}

	bool operator==(const StoredAnchor &other) const
	{
		return seg == other.seg && pos == other.pos;
	}

	bool operator!=(const StoredAnchor &other) const
	{
		return seg != other.seg || pos != other.pos;
	}

	Anchor toAnchor() const
	{
		Anchor anchor;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.pos = pos;
		return anchor;
	}
};

enum class TagStatus : uint8_t
{
	Active,
	Undone,
	UnUsed,
};

struct RangeTag
{
	bool is_left{true};
	TagStatus status{TagStatus::Active};
	StoredAnchor anchor;
	StoredRangeOp *cur{nullptr};
	StatedPtr<StoredRangeOp> old{}; // bad status for unused, nullptr for initial status

	RangeTag(bool is_left, const StoredAnchor &anchor, StoredRangeOp *cur)
		: is_left(is_left), anchor(anchor), cur(cur) {}
};

struct StoredRangeOp : public StoredOperation
{
	RangeTag *left{nullptr};
	RangeTag *right{nullptr};

	StoredRangeOp(OperationType type)
		: StoredOperation(type) {}
};

struct StoredDeletion : public StoredRangeOp
{
	bool value{true};

	StoredDeletion()
		: StoredRangeOp(OperationType::Delete) {}
};

template <typename T>
struct StoredFormat : public StoredRangeOp
{
	StyleName key;
	T value;

	StoredFormat(StyleName key, T value)
		: StoredRangeOp(OperationType::Format), key(key), value(std::move(value)) {}
};

struct StoredUndo : public StoredOperation
{
	StoredOperation *target;

	StoredUndo(StoredOperation *target)
		: StoredOperation(OperationType::Undo), target(target) {}
};

struct StoredRedo : public StoredOperation
{
	StoredOperation *target;

	StoredRedo(StoredOperation *target)
		: StoredOperation(OperationType::Redo), target(target) {}
};

struct PieceInfo
{
	size_t total{0};
	size_t visible{0};

	PieceInfo operator+(const PieceInfo &other) const
	{
		return {.total = total + other.total, .visible = visible + other.visible};
	}
	PieceInfo &operator+=(const PieceInfo &other)
	{
		visible += other.visible;
		total += other.total;
		return *this;
	}
	PieceInfo &operator-=(const PieceInfo &other)
	{
		visible -= other.visible;
		total -= other.total;
		return *this;
	}
	bool operator!=(const PieceInfo &other) const
	{
		return visible != other.visible || total != other.total;
	}
};

// Segments are split into pieces according to global offsets.
// TODO: limit the length of piece, as the split operation is O(n) in the length of piece.
struct Piece
{
	Segment *seg{nullptr};
	const char *data{nullptr};
	size_t len{0};
	size_t seg_pos{0};
	StoredRangeOp *tombStone{nullptr};

	Piece() = default;
	Piece(Segment *seg)
		: seg(seg),
		  data(seg->data.get()),
		  len(seg->len),
		  seg_pos(0) {}

	bool isRemoved() const
	{
		return tombStone != nullptr;
	}

	PieceInfo size() const
	{
		return {.total = len, .visible = isRemoved() ? 0 : len};
	}

	bool operator<(const Piece &other) const
	{
		return data < other.data;
	}
};

template <uint8_t N>
class PieceTree : public Sequence<PieceInfo, Piece, N>
{
public:
	using Base = Sequence<PieceInfo, Piece, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	PieceTree(Segment *initial_segment)
	{
		auto it = this->insertBefore(this->end(), Piece(initial_segment));
		initial_segment->last_piece = &*it;
	}

	Iterator findHistory(size_t history_pos) const
	{
		return Base::find(history_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.total;
		});
	}

	Iterator find(size_t file_pos) const
	{
		return Base::find(file_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.visible;
		});
	}

	Iterator find(const StoredAnchor &anchor) const
	{
		Segment *seg = anchor.seg;

		auto seg_it = std::lower_bound(
			seg->split_child.begin(), seg->split_child.end(), anchor.pos,
			[](const Segment *p, size_t position)
		{
			return p->insert_pos <= position;
		});
		Piece *piece = seg->last_piece;
		if (seg_it < seg->split_child.end())
			piece = (*seg_it)->insert_piece;
		assert(piece->seg == seg);
		auto it = Iterator(piece);
		if (piece->seg_pos <= anchor.pos)
			return it;
		it = findHistory(it.position().total + anchor.pos - piece->seg_pos);
		assert(it->seg == seg);
		return it;
	}

	Anchor historyAnchor(size_t pos) const
	{
		Iterator it = findHistory(pos);
		if (it.isNull())
			return Anchor();
		Segment *seg = it->seg;
		Anchor anchor;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.pos = pos - it.position().total + it->seg_pos;
		return anchor;
	}

	Anchor anchor(size_t pos) const
	{
		Iterator it = find(pos);
		if (it.isNull())
			return Anchor();
		assert(it->tombStone == nullptr);
		Segment *seg = it->seg;
		Anchor anchor;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.pos = pos - it.position().visible + it->seg_pos;
		return anchor;
	}

	size_t historyPos(const StoredAnchor &anchor) const
	{
		Iterator it = find(anchor);
		return it.position().total + (anchor.pos - it->seg_pos);
	}

	Iterator insert(Segment *segment)
	{
		StoredAnchor anchor(segment->parent, segment->insert_pos);
		Iterator it = find(anchor);
		size_t pos = anchor.pos - it->seg_pos;

		Segment *parent = segment->parent;
		auto conflict_it = std::lower_bound(
			parent->split_child.begin(), parent->split_child.end(), segment,
			[](const Segment *a, const Segment *b)
		{
			if (a->insert_pos != b->insert_pos)
				return a->insert_pos < b->insert_pos;
			if (a->stamp != b->stamp)
				return a->stamp < b->stamp;
			return a->replica->id < b->replica->id;
		});
		// handle insertion ambiguity
		if (pos == 0 && parent->split_child.size() > 0)
		{
			Piece *left_half = nullptr;
			if (conflict_it == parent->split_child.begin() || (*(conflict_it - 1))->insert_pos != anchor.pos)
			{
				if (conflict_it < parent->split_child.end() && (*conflict_it)->insert_pos == anchor.pos)
				{ // case 1: this piece is before all other segments inserted at this position
					left_half = (*conflict_it)->insert_piece;
					it = Iterator(left_half);
				}
				else
				{ // case 2: there has no other segment inserted at this position,
					--it;
					left_half = &*it;
				}
			}
			else
			{ // case 3: there has one piece inserted at this position is before this
				left_half = (*(conflict_it - 1))->last_piece;
				it = Iterator(left_half);
			}
		}
		else
		{
			it = split(it, pos);
		}
		segment->insert_piece = &*it;
		parent->split_child.insert(conflict_it, segment);

		Piece new_node(segment);
		auto new_it = this->insertAfter(it, new_node);
		segment->last_piece = &*new_it;

		// TODO: get all ranges
		return new_it;
	}

	// return the left part, creates new piece even if pos == 0
	Iterator split(Iterator it, size_t pos)
	{
		assert(pos < it->len);

		size_t offset = 0;
		const char *ptr = it->data;
		utf8::advance(ptr, pos, ptr + 4 * it->len); // max 4 bytes per utf8 char
		offset = ptr - it->data;

		// new node is the left part
		Piece new_node = *it;
		new_node.len = pos;
		it->data += offset;
		it->seg_pos += pos;
		it->len -= pos;
		it.key() = it->size(); // no need to update(), insertBefore() will do it

		return this->insertBefore(it, new_node);
	}
};

template <typename T, uint8_t N>
class RangeTree : protected OrderedSet<RangeTag, N>
{
public:
	using Base = OrderedSet<RangeTag, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	RangeTree() = default;
	~RangeTree() = default;

	template <typename PieceTree>
	auto apply(RangeTag left, RangeTag right, PieceTree &piece_tree)
	{
		// left and right can be on the same piece, so we need to split right first
		auto end = this->addTag(right, piece_tree);
		auto begin = this->addTag(left, piece_tree);
		return std::make_pair(begin, end);
	}

protected:
	template <typename PieceTree>
	auto addTag(RangeTag tag, PieceTree &piece_tree)
	{
		auto piece_it = piece_tree.find(tag.anchor);
		size_t pos = tag.anchor.pos - piece_it->seg_pos;
		if (pos != 0)
			piece_it = ++piece_tree.split(piece_it, pos);

		size_t history_pos = piece_it.position().total;

		auto it = this->insert(std::move(tag),
							   [&piece_tree, history_pos](const RangeTag &a, const RangeTag &b)
		{
			if (a.anchor.seg == b.anchor.seg)
			{
				if (a.anchor.pos != b.anchor.pos)
					return a.anchor.pos < b.anchor.pos;
			}
			else
			{
				size_t a_pos = piece_tree.historyPos(a.anchor);
				if (a_pos != history_pos)
					return a_pos < history_pos;
			}
			// new right tag-----  -----new left tag
			// old right tag--- |  | ---old left tag
			//  (prev piece]  | |  | |  [next piece)
			// -------------------------- covered old range op
			if (a.is_left != b.is_left)
				return b.is_left;
			else if (a.is_left)
				return *b.cur < *a.cur;
			else
				return *a.cur < *b.cur;
		});
		return std::make_pair(it, piece_it);
	}
};

class PieceCRDT
{
private:
	uint32_t lamport_stamp;
	const ReplicaID local_id;
	const ReplicaID origin_id;

protected:
	OrderedSet<Replica, 4> replicas;
	PieceTree<4> piece_tree;
	RangeTree<bool, 4> deletions;

public:
	using Iterator = typename PieceTree<4>::Iterator;

	PieceCRDT(const ReplicaID &origin = {})
		: lamport_stamp(0),
		  local_id(generateReplicaID()),
		  origin_id(origin.is_nil() ? local_id : origin),
		  piece_tree(storeOp<Segment>(ReplicaID(), 1, std::string(1, 0))) // EOF
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

	auto size() const
	{
		return (--piece_tree.end()).position().visible;
	}

	std::string toString() const
	{
		std::string res;
		res.reserve(size());
		for (auto it = piece_tree.begin(), end_it = --piece_tree.end(); it != end_it; ++it)
		{
			if (it->isRemoved())
				continue;
			res.append(it->data, it->len);
		}
		return res;
	}

	// anchor at visible position
	auto anchor(size_t pos) const
	{
		return piece_tree.anchor(pos);
	}

	size_t pos(const Anchor &anchor) const
	{
		StoredAnchor stored = toStored(anchor);
		if (stored.seg == nullptr)
			return std::string::npos;
		auto it = piece_tree.find(stored);
		// it.position().visible is the start of the piece, add the pos within the piece
		return it.position().visible + (anchor.pos - it->seg_pos);
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

			for (size_t i = start_stamp; i < replica.operations.size(); ++i)
			{
				const auto *stored = replica.operations[i].get();
				if (!stored)
					continue;

				switch (stored->type)
				{
				case OperationType::Insert:
				{
					const auto *seg = static_cast<const Segment *>(stored);
					assert(seg->parent != nullptr);
					const auto *parent = seg->parent;
					Anchor anchor(parent->operationID(), seg->insert_pos);
					res.push_back(std::make_unique<Insertion>(replica.id, i, anchor, std::string(seg->data.get())));
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
			return insert(static_cast<const Insertion &>(op));
		case OperationType::Delete:
			return del(static_cast<const Deletion &>(op));
		case OperationType::Undo:
			return undo(static_cast<const UndoOperation &>(op));
		case OperationType::Redo:
			return redo(static_cast<const RedoOperation &>(op));
		default:
			return false;
		}
	}

	bool insert(const Insertion &op)
	{
		auto anchor = toStored(op.anchor);
		if (anchor.seg == nullptr)
			return false; // invalid anchor

		Segment *segment = storeOp<Segment>(op.replica, op.stamp, op.str);
		if (segment == nullptr)
			return false; // duplicate operation

		segment->parent = anchor.seg;
		segment->insert_pos = anchor.pos;
		piece_tree.insert(segment);
		return true;
	}

	bool del(const Deletion &op)
	{
		auto begin = toStored(op.begin);
		auto end = toStored(op.end);
		if (begin.seg == nullptr || end.seg == nullptr)
			return false; // invalid anchor

		auto *stored_op = storeOp<StoredDeletion>(op.replica, op.stamp);
		if (!stored_op)
			return false; // duplicate operation

		auto [left, right] = deletions.apply(
			RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);

		auto [left_it, left_piece] = left;
		auto piece_before = left_piece;
		if (piece_before != piece_tree.begin())
		{
			--piece_before;
			auto op = piece_before->tombStone;
			assert(op == nullptr || op->right->old.isGood());
			if (op == nullptr)
				left_it->old = nullptr;
			else if (op->right->anchor != begin)
			{
				if (*op < *stored_op)
					left_it->old = op;
			}
			else if (op->right->old == nullptr || *op->right->old < *stored_op)
			{
				assert(op->right->status == TagStatus::Active && "tombStone should be Active");
				left_it->old = op->right->old;
			}
		}

		auto [right_it, right_piece] = right;
		auto piece_after = right_piece;
		if (piece_after != piece_tree.end())
		{
			auto op = piece_after->tombStone;
			assert(op == nullptr || op->left->old.isGood());
			if (op == nullptr)
				right_it->old = nullptr;
			else if (op->left->anchor != end)
			{
				if (*op < *stored_op)
					right_it->old = op;
			}
			else if (op->left->old == nullptr || *op->left->old < *stored_op)
			{
				assert(op->left->status == TagStatus::Active && "tombStone should be Active");
				right_it->old = op->left->old;
			}
		}

		stored_op->left = &*left_it;
		stored_op->right = &*right_it;

		// TODO: no need to redo if op is local change.
		redoRangeOp(stored_op, [](Piece *piece, StoredRangeOp *op)
		{
			if (piece->tombStone == nullptr || *piece->tombStone < *op)
				piece->tombStone = op;
		});
		piece_tree.update(left_piece, right_piece);
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
		if (target->has_undo)
			return false;
		if (target->type == OperationType::Undo)
		{
			target->has_undo = true;
			target = static_cast<StoredUndo *>(target)->target;
			return redo(RedoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type == OperationType::Redo)
		{
			target->has_undo = true;
			target = static_cast<StoredRedo *>(target)->target;
		}
		auto *undo_op = storeOp<StoredUndo>(op.replica, op.stamp, target);
		if (!undo_op)
			return false;
		undoOp(target);
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
		if (!target->has_undo)
			return false;
		if (target->type == OperationType::Undo)
		{
			target->has_undo = false;
			target = static_cast<StoredUndo *>(target)->target;
			return undo(UndoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type == OperationType::Redo)
		{
			target->has_undo = false;
			target = static_cast<StoredRedo *>(target)->target;
		}
		auto *redo_op = storeOp<StoredRedo>(op.replica, op.stamp, target);
		if (!redo_op)
			return false;
		redoOp(target);
		return true;
	}

private:
	void redoOp(StoredOperation *target)
	{
		switch (target->type)
		{
		case OperationType::Insert:
			redoInsertion(static_cast<Segment *>(target));
			break;
		case OperationType::Delete:
			redoDel(static_cast<StoredDeletion *>(target));
			break;
		case OperationType::Undo:
		case OperationType::Redo:
			assert(false && "cannot redo an undo/redo operation directly");
			break;
		default:
			break;
		}
	}

	void undoOp(StoredOperation *target)
	{
		switch (target->type)
		{
		case OperationType::Insert:
			undoInsertion(static_cast<Segment *>(target));
			break;
		case OperationType::Delete:
			undoDel(static_cast<StoredDeletion *>(target));
			break;
		case OperationType::Undo:
		case OperationType::Redo:
			assert(false && "cannot undo an undo/redo operation directly");
			break;
		default:
			break;
		}
	}

	void redoDel(StoredDeletion *target)
	{
		redoRangeOp(target, [](Piece *piece, StoredRangeOp *op)
		{
			if (piece->tombStone == nullptr || *piece->tombStone < *op)
				piece->tombStone = static_cast<StoredRangeOp *>(op);
		});

		auto left_piece = piece_tree.find(target->left->anchor);
		auto right_piece = piece_tree.find(target->right->anchor);
		piece_tree.update(&*left_piece, &*right_piece);
		target->has_undo = false;
	}

	void undoDel(StoredDeletion *target)
	{
		auto ops_covered = undoRangeOp(target, [target](Piece *piece, StoredRangeOp *newest)
		{
			if (piece->tombStone == target)
				piece->tombStone = static_cast<StoredRangeOp *>(newest);
		});

		for (auto ops : ops_covered)
		{
			redoRangeOp(ops, [](Piece *piece, StoredRangeOp *op)
			{
				if (piece->tombStone == nullptr || *piece->tombStone < *op)
					piece->tombStone = static_cast<StoredRangeOp *>(op);
			});
		}

		auto left_piece = piece_tree.find(target->left->anchor);
		auto right_piece = piece_tree.find(target->right->anchor);
		piece_tree.update(&*left_piece, &*right_piece);
		target->has_undo = true;
	}

	void redoInsertion(Segment *target)
	{
		if (target->undo_op != nullptr)
			undoDel(target->undo_op.get());
		target->has_undo = false;
	}

	void undoInsertion(Segment *target)
	{
		if (target->undo_op == nullptr)
		{
			auto stored_op = new StoredDeletion();
			stored_op->replica = target->replica;
			stored_op->stamp = target->stamp;

			auto begin = StoredAnchor(target, 0);
			auto end = StoredAnchor(target, target->len - 1);
			auto [left, right] = deletions.apply(
				RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
			auto [left_it, left_piece] = left;
			auto [right_it, right_piece] = right;
			stored_op->left = &*left_it;
			stored_op->right = &*right_it;

			target->undo_op.reset(stored_op);
		}
		redoDel(target->undo_op.get());
		target->has_undo = true;
	}

	// won't update tag->old if it is not nullptr
	template <typename UpdateFunc>
	void redoRangeOp(StoredRangeOp *stored_op, const UpdateFunc &updateFunc)
	{
		// TODO: handle left->old and right->old update
		stored_op->has_undo = false;
		auto left_it = decltype(deletions)::Iterator(stored_op->left);
		auto right_it = decltype(deletions)::Iterator(stored_op->right);

		bool has_across = false;
		auto first_across = left_it;
		auto last_across = right_it;
		auto begin_piece = piece_tree.find(stored_op->left->anchor);
		// find and update all acrossing tags
		auto it = left_it;
		for (++it;; ++it)
		{
			for (; begin_piece->seg != it->anchor.seg || begin_piece->seg_pos != it->anchor.pos; ++begin_piece)
			{
				updateFunc(&*begin_piece, stored_op);
			}
			if (it == right_it)
				break;

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
				// this can happen when it has a common begin/end with other ops
				// TODO: we can apply it instead of marking UnUsed
				// assert(left_it->old.isBad() && right_it->old.isBad());
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
		stored_op->has_undo = true;
		auto left_it = decltype(deletions)::Iterator(stored_op->left);
		auto right_it = decltype(deletions)::Iterator(stored_op->right);

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
			for (; begin_piece->seg != it->anchor.seg || begin_piece->seg_pos != it->anchor.pos; ++begin_piece)
			{
				updateFunc(&*begin_piece, newest);
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

		// try to apply all covered ops, from newest to oldest
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
		if (it ==  replicas.end() || it->id != id)
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
		if (!seg_ptr || seg_ptr->type != OperationType::Insert)
			return StoredAnchor();

		return StoredAnchor(static_cast<Segment *>(seg_ptr.get()), anchor.pos);
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
