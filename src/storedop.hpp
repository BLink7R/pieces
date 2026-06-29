#pragma once

#include <cassert>
#include <utf8cpp/utf8.h>

#include "crdt.hpp"
#include "taggedptr.hpp"

struct Replica;
struct Piece;
struct StoredAnchor;
struct Segment;
struct StoredOperation;
struct StoredRangeOp;
struct StoredDeletion;

using piece_size_t = int32_t;
using stamp_size_t = uint32_t;
using doc_size_t = size_t;

struct Replica
{
	ReplicaID id{};
	// TODO: change to map to save space
	mutable std::vector<std::unique_ptr<StoredOperation>> operations; // created segments

	stamp_size_t maxStamp() const
	{
		if (operations.empty())
			return 0;
		return static_cast<stamp_size_t>(operations.size());
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

struct StoredAnchor
{
	Segment *seg{nullptr};
	piece_size_t pos{0};

	StoredAnchor() = default;
	StoredAnchor(Segment *seg, piece_size_t pos)
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
	piece_size_t segPos() const;
	Anchor toAnchor() const;
};

struct StoredOperation
{
	const Replica *replica{nullptr};
	stamp_size_t stamp{0};

	virtual ~StoredOperation() = default;

	virtual OperationType type() const = 0;

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

	bool hasUndo() const
	{
		if (undoredo == nullptr)
			return false;
		return undoredo->type() == OperationType::Undo;
	}
};

// Undo/redo operations can not be undone/redone again, undo/redo of them
// will be transformed to undo/redo of their target operations.
struct StoredUndo : public StoredOperation
{
	UndoRedoableOp *target;

	StoredUndo(UndoRedoableOp *target)
		: StoredOperation(), target(target) {}

	OperationType type() const override
	{
		return OperationType::Undo;
	}
};

struct StoredRedo : public StoredOperation
{
	UndoRedoableOp *target;

	StoredRedo(UndoRedoableOp *target)
		: StoredOperation(), target(target) {}

	OperationType type() const override
	{
		return OperationType::Redo;
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
};

struct StoredDeletion : public StoredRangeOp
{
	OperationType type() const override
	{
		return OperationType::Delete;
	}
};

// Text is stored in segments. Whenever text is inserted, a new segment is created,
// and the target segment with the insertion offset is stored, keeping the target unchanged.
// As user edits are usually small, and we need to implement anchor in 2 directions, we
// limit the length of segments to INT_MAX. The length of pieces can be much smaller.
struct Segment : public UndoRedoableOp
{
	StoredAnchor anchor;
	int len;
	mutable std::vector<Segment *> child;					   // as segments are usually small, vector is faster
	mutable std::unique_ptr<StoredDeletion> undo_op{nullptr}; // when insertion is undone, it needs an extra deletion
	mutable std::vector<Piece *> split_piece;
	std::unique_ptr<const char[]> data{nullptr};
	std::unique_ptr<piece_size_t[]> line_breaks{nullptr};
	piece_size_t line_break_count{0};

	Segment(const std::string &str)
		: UndoRedoableOp()
	{
		// TODO: ensure that str.size() <= INT32_MAX
		data = std::make_unique<const char[]>(str.size() + 1);
		memcpy(const_cast<char *>(data.get()), str.c_str(), str.size() + 1);
		len = static_cast<piece_size_t>(utf8::distance(data.get(), data.get() + str.size()));

		// collect newline positions in UTF-8 character offsets
		if (!str.empty())
		{
			std::vector<piece_size_t> breaks;
			const char *it = str.data();
			const char *end = it + str.size();
			piece_size_t char_index = 0;
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
				line_breaks = std::make_unique<piece_size_t[]>(line_break_count);
				for (size_t i = 0; i < line_break_count; ++i)
					line_breaks[i] = breaks[i];
			}
		}
	}
	~Segment() override = default;

	OperationType type() const override
	{
		return OperationType::Insert;
	}

	auto pieceAt(piece_size_t position) const;
	auto insertPiece(Piece *piece);

	auto insertSegment(Segment *segment)
	{ // TODO: we can change it to std::find if segment is small
		auto it = std::lower_bound(
			child.begin(), child.end(), segment,
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
		return child.insert(it, segment);
	}

	piece_size_t findLineBreak(piece_size_t utf8_offset) const
	{
		if (line_break_count == 0)
			return 0;
		const piece_size_t *begin = line_breaks.get();
		const piece_size_t *end = begin + line_break_count;
		const piece_size_t *it = std::lower_bound(begin, end, utf8_offset);
		return static_cast<piece_size_t>(it - begin);
	}

	Segment(Segment &&other) noexcept = default;
	Segment &operator=(Segment &&other) noexcept = default;
	Segment(const Segment &other) = delete;
	Segment &operator=(const Segment &other) = delete;
};

struct PieceInfo
{
	doc_size_t total{0};
	doc_size_t visible{0};

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
	piece_size_t len{0};
	piece_size_t seg_pos{0};
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
		return {.total = static_cast<doc_size_t>(len),
				.visible = isRemoved() ? 0 : static_cast<doc_size_t>(len)};
	}

	bool operator<(const Piece &other) const
	{
		return data < other.data;
	}
};

// if anchor is normal, return the piece before the position
// if anchor is reversed, return the piece after the position
inline auto Segment::pieceAt(piece_size_t pos) const
{
	if (pos == 0 || std::abs(pos) > len)
		return split_piece.end();
	if (pos > 0)
		return std::lower_bound(
			split_piece.begin(), split_piece.end(), pos,
			[](const Piece *p, piece_size_t position)
		{
			return p->seg_pos + p->len < position;
		});
	return std::lower_bound(
		split_piece.begin(), split_piece.end(), len + pos,
		[](const Piece *p, piece_size_t position)
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

inline Anchor StoredAnchor::toAnchor() const
{
	Anchor anchor;
	anchor.replica = seg->replica->id;
	anchor.stamp = seg->stamp;
	anchor.pos = pos;
	return anchor;
}

inline piece_size_t StoredAnchor::segPos() const
{
	return pos < 0 ? seg->len + pos : pos;
}