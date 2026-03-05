#pragma once

#include <cassert>

#include "crdt.hpp"
#include "taggedptr.hpp"

struct Replica;
struct Piece;
struct StoredAnchor;
struct StoredContent;
struct StoredOperation;
struct StoredRangeOp;
struct StoredDeletion;

struct Replica
{
	ReplicaID id{};
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

struct StoredAnchor
{
	StoredContent *seg{nullptr};
	int32_t pos{0};

	StoredAnchor() = default;
	StoredAnchor(StoredContent *seg, int32_t pos)
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

struct StoredOperation
{
	const Replica *replica{nullptr};
	uint32_t stamp{0};

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

// inline shapes and tables are also StoredContents, but has a len of 1
// the derived classes must have a len >= 1
struct StoredContent : public UndoRedoableOp
{
	StoredAnchor anchor;
	int len;
	mutable std::vector<StoredContent *> child;				   // as segments are usually small, vector is faster
	mutable std::unique_ptr<StoredDeletion> undo_del{nullptr}; // when insertion is undone, it needs an extra deletion

	OperationType type() const override
	{
		return OperationType::Insert;
	}
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

	OperationType type() const override
	{
		return OperationType::RangeFormat;
	}

	virtual int styleType() const = 0;
};

struct StoredDeletion : public StoredRangeOp
{
	OperationType type() const override
	{
		return OperationType::Delete;
	}

	int styleType() const override
	{
		return 0;
	}
};

template <typename T>
struct StoredFormat : public StoredRangeOp
{
	int key;
	T value;

	StoredFormat(int key, T value)
		: StoredRangeOp(), key(key), value(std::move(value)) {}

	int styleType() const override
	{
		return key;
	}
};

// format of a paragraph, such as heading, list
template <typename T>
struct ParaFormat : public UndoRedoableOp
{
	StoredAnchor anchor;
	int key;
	T value;
};