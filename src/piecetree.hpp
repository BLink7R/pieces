#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
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
		return static_cast<uint32_t>(operations.size());
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

struct UndoRedoableOp : public StoredOperation
{
	StoredOperation *undoredo{nullptr}; // newest undo/redo operation

	UndoRedoableOp(OperationType type)
		: StoredOperation(type) {}

	bool hasUndo() const
	{
		if (undoredo == nullptr)
			return false;
		return undoredo->type == OperationType::Undo;
	}
};

struct StoredAnchor
{
	Segment *seg{nullptr};
	int32_t pos{0};

	StoredAnchor() = default;
	StoredAnchor(Segment *seg, int32_t pos)
		: seg(seg), pos(pos)
	{
		assert(seg != nullptr && pos != 0);
	}

	bool isNull() const
	{
		return pos == 0;
	}

	bool isReversed() const
	{
		return pos < 0;
	}

	bool operator==(const StoredAnchor &other) const
	{
		return seg == other.seg && pos == other.pos;
	}

	bool operator!=(const StoredAnchor &other) const
	{
		return seg != other.seg || pos != other.pos;
	}

	// return distance to segment start
	int32_t segPos() const;
	Anchor toAnchor() const;
};

// Text is stored in segments. Whenever text is inserted, a new segment is created,
// and the target segment with the insertion offset is stored, keeping the target unchanged.
// As user edits are usually small, and we need to implement anchor in 2 directions, we
// limit the length of segments to INT_MAX. The length of pieces can be much smaller.
struct Segment : public UndoRedoableOp
{
	StoredAnchor anchor;
	mutable std::vector<Piece *> split_piece;
	mutable std::vector<Segment *> split_child; // as segments are usually small, vector is faster
	std::unique_ptr<const char[]> data{nullptr};
	int32_t len{0};
	std::unique_ptr<size_t[]> line_breaks{nullptr};
	size_t line_break_count{0};
	std::unique_ptr<StoredDeletion> undo_op{nullptr};

	Segment(const std::string &str)
		: UndoRedoableOp(OperationType::Insert)
	{
		// TODO: ensure that str.size() <= INT32_MAX
		data = std::make_unique<const char[]>(str.size() + 1);
		memcpy(const_cast<char *>(data.get()), str.c_str(), str.size() + 1);
		len = static_cast<int32_t>(utf8::distance(data.get(), data.get() + str.size()));

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

	auto pieceAt(int32_t position) const;
	auto insertPiece(Piece *piece);

	auto insertSegment(Segment *segment)
	{ // TODO: we can change it to std::find if segment is small
		auto it = std::lower_bound(
			split_child.begin(), split_child.end(), segment,
			[](const Segment *a, const Segment *b)
		{
			if (a->anchor.segPos() != b->anchor.segPos())
				return a->anchor.segPos() < b->anchor.segPos();
			if (a->anchor.pos != b->anchor.pos) // reversed vs normal, normal goes first
				return a->anchor.pos > b->anchor.pos;
			if (a->anchor.pos > 0)
				return *a < *b; // normal: earlier goes first
			else
				return *b < *a; // reversed: later goes first
		});
		return split_child.insert(it, segment);
	}

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

inline Anchor StoredAnchor::toAnchor() const
{
	Anchor anchor;
	anchor.replica = seg->replica->id;
	anchor.stamp = seg->stamp;
	anchor.pos = pos;
	return anchor;
}

inline int32_t StoredAnchor::segPos() const
{
	return pos < 0 ? seg->len + pos : pos;
}

enum class TagStatus : uint8_t
{
	Active,
	Undone,
	UnUsed,
};

struct RangeTag
{
	bool is_left{true};
	TagStatus status{TagStatus::Undone};
	StoredAnchor anchor;
	StoredRangeOp *cur{nullptr};
	StatedPtr<StoredRangeOp> old{}; // bad status for unused, nullptr for initial status

	RangeTag(bool is_left, const StoredAnchor &anchor, StoredRangeOp *cur)
		: is_left(is_left), anchor(anchor), cur(cur) {}
};

struct StoredRangeOp : public UndoRedoableOp
{
	RangeTag *left{nullptr};
	RangeTag *right{nullptr};

	StoredRangeOp(OperationType type)
		: UndoRedoableOp(type) {}
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

// Undo/redo operations can not be undone/redone again, undo/redo of them
// will be transformed to undo/redo of their target operations.
struct StoredUndo : public StoredOperation
{
	UndoRedoableOp *target;

	StoredUndo(UndoRedoableOp *target)
		: StoredOperation(OperationType::Undo), target(target) {}
};

struct StoredRedo : public StoredOperation
{
	UndoRedoableOp *target;

	StoredRedo(UndoRedoableOp *target)
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
	int32_t len{0};
	int32_t seg_pos{0};
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
		return {.total = static_cast<size_t>(len),
				.visible = isRemoved() ? 0 : static_cast<size_t>(len)};
	}

	bool operator<(const Piece &other) const
	{
		return data < other.data;
	}
};

// if anchor is normal, return the piece before the position
// if anchor is reversed, return the piece after the position
inline auto Segment::pieceAt(int32_t pos) const
{
	if (pos == 0 || std::abs(pos) > len)
		return split_piece.end();
	if (pos > 0)
		return std::lower_bound(
			split_piece.begin(), split_piece.end(), pos,
			[](const Piece *p, size_t position)
		{
			return p->seg_pos + p->len < position;
		});
	return std::lower_bound(
		split_piece.begin(), split_piece.end(), len + pos,
		[](const Piece *p, size_t position)
	{
		return p->seg_pos + p->len <= position;
	});
}

inline auto Segment::insertPiece(Piece *piece)
{
	auto it = std::lower_bound(
		split_piece.begin(), split_piece.end(), piece,
		[](const Piece *a, const Piece *b)
	{
		return a->seg_pos < b->seg_pos;
	});
	return split_piece.insert(it, piece);
}

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
		initial_segment->split_piece.push_back(&*it);
	}

	Iterator findHistory(size_t history_pos) const
	{
		return Base::find(history_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.total;
		});
	}

	// Finds the first piece with end position > file_pos
	Iterator upper_bound(size_t file_pos) const
	{
		return Base::find(file_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.visible;
		});
	}

	// Finds the first piece with end position >= file_pos
	Iterator lower_bound(size_t file_pos) const
	{
		return Base::find(file_pos, [](size_t a, const PieceInfo &b)
		{
			return a <= b.visible;
		});
	}

	// if anchor is normal, return the piece before the position
	// if anchor is reversed, return the piece after the position
	Iterator find(const StoredAnchor &anchor) const
	{
		Segment *seg = anchor.seg;
		auto piece_it = seg->pieceAt(anchor.pos);
		if (piece_it == seg->split_piece.end())
			return this->end();
		return Iterator(*piece_it);
	}

	size_t historyPos(const StoredAnchor &anchor) const
	{
		Iterator it = find(anchor);
		if (anchor.pos == anchor.seg->len)
			return it.position().total;
		return it.position().total + (anchor.segPos() - it->seg_pos);
	}

	Iterator insert(Segment *segment)
	{
		StoredAnchor anchor = segment->anchor;
		Segment *parent = anchor.seg;
		assert(parent != nullptr);
		auto piece_it = find(anchor);
		assert(piece_it != this->end());
		int32_t pos = static_cast<int32_t>(anchor.segPos() - piece_it->seg_pos);
		auto conflict_it = parent->insertSegment(segment);
		Piece new_node(segment);
		// handle insertion ambiguity
		if (pos == 0)
		{ // for reversed anchor
			Piece *piece = &*piece_it;
			if (conflict_it + 1 != parent->split_child.end())
			{
				Segment *after = *(conflict_it + 1);
				if (after->anchor.pos == anchor.pos) // has conflict
				{
					for (; !after->split_child.empty(); after = after->split_child[0])
					{
						if (after->split_child[0]->anchor.pos != 0)
							break;
					}
					piece = after->split_piece[0];
				}
			}
			piece_it = this->insertBefore(Iterator(piece), new_node);
		}
		else if (pos == piece_it->len)
		{ // for normal anchor
			Piece *piece = &*piece_it;
			if (conflict_it != parent->split_child.begin())
			{
				Segment *before = *(conflict_it - 1);
				if (before->anchor.pos == anchor.pos) // has conflict
				{
					for (; !before->split_child.empty(); before = before->split_child.back())
					{
						if (before->split_child.back()->anchor.pos != before->len)
							break;
					}
					piece = before->split_piece.back();
				}
			}
			piece_it = this->insertAfter(Iterator(piece), new_node);
		}
		else
		{
			piece_it = split(piece_it, pos);
			piece_it = this->insertBefore(piece_it, new_node);
		}
		segment->insertPiece(&*piece_it);
		return piece_it;
	}

	// return the right part
	Iterator split(Iterator it, int32_t pos)
	{
		assert(0 < pos && pos < it->len);

		size_t offset = 0;
		const char *ptr = it->data;
		utf8::advance(ptr, pos, ptr + 4 * it->len); // max 4 bytes per utf8 char
		offset = ptr - it->data;

		// new node is the right part
		Piece new_node = *it;
		it->len = pos;
		new_node.data += offset;
		new_node.seg_pos += pos;
		new_node.len -= pos;
		it.key() = it->size(); // no need to update(), insertBefore() will do it
		this->update(it, it);

		auto new_it = this->insertAfter(it, new_node);
		it->seg->insertPiece(&*new_it);
		return new_it;
	}
};

template <typename T, RangeInterval RightOpen, uint8_t N>
class RangeTree : public OrderedSet<RangeTag, N>
{
public:
	using Base = OrderedSet<RangeTag, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	RangeTree() = default;
	~RangeTree() = default;

	// should ganrantee left.anchor < right.anchor
	template <typename PieceTree>
	auto apply(RangeTag left, RangeTag right, PieceTree &piece_tree)
	{
		// left and right can be on the same piece, so we need to split right first
		auto begin = this->addTag<true>(left, piece_tree);
		auto end = this->addTag<false>(right, piece_tree);
		return std::make_pair(begin, end);
	}

protected:
	template <bool IsLeft, typename PieceTree>
	auto addTag(RangeTag tag, PieceTree &piece_tree)
	{
		auto piece_it = piece_tree.find(tag.anchor);
		int32_t pos = tag.anchor.segPos() - piece_it->seg_pos;
		if constexpr (IsLeft || RightOpen == RangeInterval::Exclusive)
		{ // reversed anchor, anchor.pos is in [-segment.len, -1]
			assert(0 < -tag.anchor.pos && tag.anchor.segPos() <= tag.anchor.segPos());
			if (pos > 0)
				piece_it = piece_tree.split(piece_it, pos);
		}
		else
		{ // for right closed anchor, anchor.pos is in [1, segment.len]
			assert(0 < tag.anchor.pos && tag.anchor.pos <= tag.anchor.seg->len);
			if (pos == piece_it->len)
				++piece_it;
			else
				piece_it = piece_tree.split(piece_it, pos);
		}

		size_t history_pos = piece_it.position().total;

		auto it = this->insert(std::move(tag),
							   [&piece_tree, history_pos](const RangeTag &a, const RangeTag &b)
		{
			size_t a_pos = piece_tree.historyPos(a.anchor);
			if (a_pos != history_pos)
				return a_pos < history_pos;
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
	mutable PieceTree<4> piece_tree;
	RangeTree<bool, RangeInterval::Inclusive, 4> deletions;

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
			// std::cout << std::string(it->data, it->len) << " " << it->isRemoved() << "\n";
			if (it->isRemoved())
				continue;
			res.append(it->data, it->len);
		}
		return res;
	}

	Anchor anchor(size_t pos) const
	{
		Anchor anchor;
		if (pos == 0)
			return anchor;
		auto it = piece_tree.lower_bound(pos);
		if (it.isNull())
			return anchor;
		Segment *seg = it->seg;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.pos = static_cast<int32_t>(pos - it.position().visible + it->seg_pos);
		return anchor;
	}

	Anchor reversedAnchor(size_t pos) const
	{
		Anchor anchor;
		auto it = piece_tree.upper_bound(pos);
		if (it.isNull())
			return anchor;
		Segment *seg = it->seg;
		anchor.replica = seg->replica->id;
		anchor.stamp = seg->stamp;
		anchor.pos = static_cast<int32_t>(pos - it.position().visible + it->seg_pos - seg->len);
		return anchor;
	}

	// implement the [Fugue](https://arxiv.org/abs/2305.00583) algorithm to solve the interleaving problem.
	// brief of this algorithm: always attach the insertion to the newer piece at left/right.
	Anchor insertAnchor(size_t pos) const
	{
		auto it = piece_tree.upper_bound(pos);
		Anchor anchor;
		if (pos > 0 && pos == it.position().visible)
		{ // as begining of the piece
			Segment *seg_right = it->seg;
			auto it_before = it;
			--it_before;
			Segment *seg_left = it_before->seg;
			if (seg_left == seg_right || *seg_left < *seg_right)
			{ // right is newer
				anchor.replica = seg_right->replica->id;
				anchor.stamp = seg_right->stamp;
				anchor.pos = it->seg_pos - seg_right->len; // reversed anchor
			}
			else
			{ // left is newer
				anchor.replica = seg_left->replica->id;
				anchor.stamp = seg_left->stamp;
				anchor.pos = it_before->seg_pos + it_before->len; // normal anchor
			}
		}
		else
		{ // when inside the piece, we prefer to insert before the character
			Segment *seg = it->seg;
			anchor.replica = seg->replica->id;
			anchor.stamp = seg->stamp;
			anchor.pos = static_cast<int32_t>(pos - it.position().visible + it->seg_pos - seg->len); // reversed anchor
		}
		return anchor;
	}

	size_t pos(const Anchor &anchor) const
	{
		StoredAnchor stored = toStored(anchor);
		if (stored.seg == nullptr)
			return std::string::npos;
		auto it = piece_tree.find(stored);
		if (anchor.pos == stored.seg->len)
			return it.position().visible;
		// it.position().visible is the start of the piece, add the pos within the piece
		return it.position().visible + (stored.segPos() - it->seg_pos);
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

				switch (stored->type)
				{
				case OperationType::Insert:
				{
					const auto *seg = static_cast<const Segment *>(stored);
					assert(seg->anchor.seg != nullptr);
					const auto *parent = seg->anchor.seg;
					Anchor anchor(parent->operationID(), seg->anchor.pos);
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
		if (op.str.empty())
			return false; // no-op
		auto anchor = toStored(op.anchor);
		if (anchor.seg == nullptr)
			return false; // invalid anchor

		Segment *segment = storeOp<Segment>(op.replica, op.stamp, op.str);
		if (segment == nullptr)
			return false; // duplicate operation

		segment->anchor = anchor;
		piece_tree.insert(segment);
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

		auto [left, right] = deletions.apply(
			RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
		auto [left_it, left_piece] = left;
		auto [right_it, right_piece] = right;
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
		if (target->type == OperationType::Undo)
		{
			target = static_cast<StoredUndo *>(target)->target;
			return redo(RedoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type == OperationType::Redo)
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
		if (target->type == OperationType::Undo)
		{
			target = static_cast<StoredUndo *>(target)->target;
			return undo(UndoOperation(op.replica, op.stamp, OperationID{target->replica->id, target->stamp}));
		}
		if (target->type == OperationType::Redo)
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
		target->undoredo = op;
	}

	void undoOp(StoredUndo *op)
	{
		UndoRedoableOp *target = op->target;
		if (target->undoredo && *op < *target->undoredo)
			return; // LWW - last write wins
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

		redoRangeOp(target, [](Piece *piece, StoredRangeOp *op)
		{
			if (piece->tombStone == nullptr || *piece->tombStone < *op)
				piece->tombStone = static_cast<StoredRangeOp *>(op);
		});
		piece_tree.update(left_piece, right_piece);
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
		piece_tree.update(left_piece, right_piece);
	}

	void redoInsertion(Segment *target)
	{
		if (target->undo_op != nullptr)
			undoDel(target->undo_op.get());
	}

	void undoInsertion(Segment *target)
	{
		if (target->undo_op == nullptr)
		{
			auto stored_op = new StoredDeletion();
			stored_op->replica = target->replica;
			stored_op->stamp = target->stamp;

			auto begin = StoredAnchor(target, -target->len);
			auto end = StoredAnchor(target, target->len);
			auto [left, right] = deletions.apply(
				RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), piece_tree);
			auto [left_it, left_piece] = left;
			auto [right_it, right_piece] = right;
			stored_op->left = &*left_it;
			stored_op->right = &*right_it;

			target->undo_op.reset(stored_op);
		}
		redoDel(target->undo_op.get());
	}

	// won't update tag->old if it is not nullptr
	template <typename UpdateFunc>
	void redoRangeOp(StoredRangeOp *stored_op, const UpdateFunc &updateFunc)
	{
		auto left_it = decltype(deletions)::Iterator(stored_op->left);
		auto right_it = decltype(deletions)::Iterator(stored_op->right);

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
			if (it->anchor.isReversed())
			{
				for (; begin_piece->seg != it->anchor.seg || begin_piece->seg_pos != it->anchor.segPos(); ++begin_piece)
				{
					updateFunc(&*begin_piece, newest);
				}
			}
			else
			{
				for (;; ++begin_piece)
				{
					updateFunc(&*begin_piece, newest);
					if (begin_piece->seg == it->anchor.seg && begin_piece->seg_pos + begin_piece->len == it->anchor.pos)
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
		if (!seg_ptr || seg_ptr->type != OperationType::Insert)
			return StoredAnchor();

		Segment *seg = static_cast<Segment *>(seg_ptr.get());
		if (anchor.pos == 0 || seg->len < std::abs(anchor.pos))
			return StoredAnchor();

		return StoredAnchor(seg, anchor.pos);
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
