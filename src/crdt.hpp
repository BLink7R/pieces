#pragma once

#include <cstdint>
#include <string>

#include <stduuid/uuid.h>

// guid
using ReplicaID = uuids::uuid;

inline ReplicaID generateReplicaID()
{
	static thread_local std::mt19937 generator(std::random_device{}());
	return uuids::uuid_random_generator(generator)();
}

struct OperationID
{
	ReplicaID replica{};
	uint32_t stamp{0};

	bool operator<(const OperationID &other) const
	{
		if (replica != other.replica)
			return replica < other.replica;
		return stamp < other.stamp;
	}

	bool operator!=(const OperationID &other) const
	{
		return replica != other.replica || stamp != other.stamp;
	}
};

enum class OperationType : uint8_t
{
	Insert,
	Delete,
	Undo,
	Redo,
};

struct Operation
{
	ReplicaID replica;
	uint32_t stamp;
	OperationType type;

	Operation() = default;
	Operation(const ReplicaID &replica, uint32_t stamp, OperationType type)
		: replica(replica), stamp(stamp), type(type) {}
};

// Anchor has 2 types: after a character (normal), before a character (reversed).
// This is represented by the sign of pos:
// 	pos > 0 - after the character at pos
// 	pos < 0 - before the character at backward |pos|
// For example: "abc"
// pos = 1 -> a]bc (after 'a')
// pos = -1 -> ab[c (before 'c')
// pos = 0 is invalid, so it requires that all segments are non-empty.
struct Anchor
{
	ReplicaID replica{};
	uint32_t stamp{0};
	int32_t pos{0};

	Anchor(ReplicaID replica = {}, uint32_t stamp = 0, int32_t pos = 0)
		: replica(replica), stamp(stamp), pos(pos) {}

	Anchor(OperationID opID, int32_t pos)
		: replica(opID.replica), stamp(opID.stamp), pos(pos) {}

	bool operator==(const Anchor &other) const
	{
		return replica == other.replica && stamp == other.stamp && pos == other.pos;
	}

	bool isNull() const
	{
		return pos == 0;
	}

	bool isReversed() const
	{
		return pos < 0;
	}
};

struct OpenedRange
{
	Anchor begin; // reversed anchor
	Anchor end;	  // reversed anchor

	OpenedRange() = default;
	OpenedRange(const Anchor &begin, const Anchor &end)
		: begin(begin), end(end) {}
};

struct ClosedRange
{
	Anchor begin; // reversed anchor
	Anchor end;	  // normal anchor

	ClosedRange() = default;
	ClosedRange(const Anchor &begin, const Anchor &end)
		: begin(begin), end(end) {}
};

struct Insertion : public Operation
{
	Anchor anchor;
	std::string str;

	Insertion() = default;
	Insertion(const ReplicaID &replica, uint32_t stamp, const Anchor &anchor, std::string text)
		: Operation(replica, stamp, OperationType::Insert), anchor(anchor), str(std::move(text)) {}
};

// All ranges are inclusive on the left side, but the right side can be inclusive or exclusive.
// Inclusive operations: deletion
enum class RangeInterval : uint8_t
{
	Inclusive,
	Exclusive,
};

struct Deletion : public Operation
{
	ClosedRange range;

	Deletion() = default;
	Deletion(const ReplicaID &replica, uint32_t stamp, const Anchor &begin, const Anchor &end)
		: Operation(replica, stamp, OperationType::Delete), range(begin, end) {}
	Deletion(const ReplicaID &replica, uint32_t stamp, const ClosedRange &range)
		: Operation(replica, stamp, OperationType::Delete), range(range) {}
};

// one replica can only undo/redo its operation, so only one stamp is needed.
struct UndoOperation : public Operation
{
	OperationID target;

	UndoOperation() = default;
	UndoOperation(const ReplicaID &replica, uint32_t stamp, const OperationID &target)
		: Operation(replica, stamp, OperationType::Undo), target(target) {}
};

struct RedoOperation : public Operation
{
	OperationID target;

	RedoOperation() = default;
	RedoOperation(const ReplicaID &replica, uint32_t stamp, const OperationID &target)
		: Operation(replica, stamp, OperationType::Redo), target(target) {}
};
