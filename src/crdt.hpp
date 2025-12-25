#pragma once

#include <cstdint>
#include <string>

#define UUID_SYSTEM_GENERATOR
#include <stduuid/uuid.h>

// guid
using ReplicaID = uuids::uuid;

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
	Format,
	Undo,
	Redo,
};

struct Operation
{
	ReplicaID replica;
	uint32_t stamp;
	OperationType type;

	Operation(const ReplicaID &replica, uint32_t stamp, OperationType type)
		: replica(replica), stamp(stamp), type(type) {}
};

struct Anchor
{
	ReplicaID replica{};
	uint32_t stamp{0};
	size_t pos{0};
};

struct Insertion : public Operation
{
	Anchor anchor;
	std::string str;

	Insertion(const ReplicaID &replica, uint32_t stamp, const Anchor &anchor, std::string text)
		: Operation(replica, stamp, OperationType::Insert), anchor(anchor), str(std::move(text))
	{
	}
};

enum class StyleName : uint8_t
{
	Hidden,
	Bold,
	Italic,
	Underline,
	Strikethrough,
	FontSize,
	FontFamily,
	Color,
	BackgroundColor,
};

struct Deletion : public Operation
{
	Anchor begin;
	Anchor end;

	Deletion(const ReplicaID &replica, uint32_t stamp, const Anchor &begin, const Anchor &end)
		: Operation(replica, stamp, OperationType::Delete), begin(begin), end(end)
	{
	}
};

template <typename T>
struct Formatting : public Operation
{
	Anchor begin;
	Anchor end;
	StyleName Key;
	T value;

	Formatting(const ReplicaID &replica, uint32_t stamp, const Anchor &begin, const Anchor &end)
		: Operation(replica, stamp, OperationType::Format), begin(begin), end(end)
	{
	}
};

// one replica can only undo/redo its operation, so only one stamp is needed.
struct UndoOperation : public Operation
{
	OperationID target;

	UndoOperation(const ReplicaID &replica, uint32_t stamp, const OperationID &target)
		: Operation(replica, stamp, OperationType::Undo), target(target)
	{
	}
};

struct RedoOperation : public Operation
{
	OperationID target;

	RedoOperation(const ReplicaID &replica, uint32_t stamp, const OperationID &target)
		: Operation(replica, stamp, OperationType::Redo), target(target)
	{
	}
};
