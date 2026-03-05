#pragma once

#include <string>
#include <utility>

#include <limits>
#include <stack>
#include <vector>

#include "textcrdt.hpp"

// TODO: check index is valid
class PlainText
{
private:
	PieceCRDT<void> doc;
	std::stack<uint32_t> undo_stack;
	std::stack<uint32_t> redo_stack;

public:
	using Iterator = PieceCRDT<void>::Iterator;

	PlainText() = default;
	PlainText(const ReplicaID &rid)
		: doc(rid) {}

	// basic information
	size_t size() const;
	// size_t rowSize() const;
	bool empty() const;

	// export
	std::string toString() const;
	std::string slice(size_t begin, size_t end) const;
	std::string slice(const Anchor &begin, const Anchor &end) const;

	// iterate
	// Iterator begin();
	// Iterator end();
	// Iterator find(size_t pos);
	// Iterator find(const Anchor &anchor);
	// Iterator find(size_t row, size_t column);

	// edit, return operation stamp
	size_t insert(size_t pos, const std::string &text);
	// size_t insert(size_t row, size_t column, const std::string &text);
	size_t insert(const Anchor &anchor, const std::string &text);
	size_t del(size_t begin, size_t end);
	// size_t del(size_t row_begin, size_t column_begin, size_t row_end, size_t column_end);
	size_t del(const ClosedRange &range);

	// index conversion
	OpenedRange toOpenedRange(size_t begin, size_t end) const;
	ClosedRange toClosedRange(size_t begin, size_t end) const;
	Anchor toAnchor(size_t pos) const;
	// Anchor toAnchor(size_t row, size_t column) const;
	// size_t toPos(size_t row, size_t column) const;
	size_t toPos(const Anchor &anchor) const;

	// undo/redo, only undo/redo local user's operations
	// the operation created WONT be recorded in undo/redo stack
	void beginGroup();
	void endGroup();
	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();

	// undo/redo specific operation, other user's operations included
	// the operation created WILL be recorded in undo/redo stack
	size_t undoSpecific(OperationID opID);
	size_t redoSpecific(OperationID opID);

	// notifiers

	// remote operations
	ReplicaID replicaID() const;
	ReplicaID origin() const;
	bool apply(const Operation &op); // applyed operation won't be recorded in undo/redo stack
	void apply(const std::vector<std::unique_ptr<Operation>> &ops);
	std::vector<OperationID> frontline();
	std::vector<std::unique_ptr<Operation>> diff(const std::vector<OperationID> &frontline = {}); // return operations ahead of the given frontline
};

template <typename FormatProvider>
class RichText
{
private:
	PieceCRDT<FormatProvider> doc;
	std::stack<uint32_t> undo_stack;
	std::stack<uint32_t> redo_stack;

	static constexpr uint32_t kGroupStart = std::numeric_limits<uint32_t>::max();
	static constexpr uint32_t kGroupEnd = std::numeric_limits<uint32_t>::max() - 1;

public:
	using Iterator = PieceCRDT<FormatProvider>::Iterator;

	RichText() = default;
	RichText(const ReplicaID &rid)
		: doc(rid) {}

	// basic information
	size_t size() const;
	bool empty() const;
	FormatProvider &formatProvider();

	// export
	std::string toString() const;
	std::string slice(size_t begin, size_t end) const;
	std::string slice(const Anchor &begin, const Anchor &end) const;

	// iterate
	// Iterator begin();
	// Iterator end();
	// Iterator find(size_t pos);
	// Iterator find(const Anchor &anchor);

	// edit, return operation stamp
	size_t insert(size_t pos, const std::string &text);
	size_t insert(const Anchor &anchor, const std::string &text);
	size_t del(size_t begin, size_t end);
	size_t del(const ClosedRange &range);
	template <typename RangeType, typename T>
	size_t format(const std::string &style_name, size_t begin, size_t end, const T &value = {});
	template <typename RangeType, typename T>
	size_t format(const std::string &style_name, RangeType range, const T &value = {});

	// style
	template <typename T>
	T style(const std::string &style_name, size_t pos) const;

	// index conversion
	OpenedRange toOpenedRange(size_t begin, size_t end) const;
	ClosedRange toClosedRange(size_t begin, size_t end) const;
	Anchor toAnchor(size_t pos) const;
	size_t toPos(const Anchor &anchor) const;

	// undo/redo, only undo/redo local user's operations
	// the operation created WONT be recorded in undo/redo stack
	void beginGroup();
	void endGroup();
	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();

	// undo/redo specific operation, other user's operations included
	// the operation created WILL be recorded in undo/redo stack
	size_t undoSpecific(OperationID opID);
	size_t redoSpecific(OperationID opID);

	// notifiers

	// remote operations
	ReplicaID replicaID() const;
	ReplicaID origin() const;
	bool apply(const Operation &op); // applied operation won't be recorded in undo/redo stack
	void apply(const std::vector<std::unique_ptr<Operation>> &ops);
	std::vector<OperationID> frontline();
	std::vector<std::unique_ptr<Operation>> diff(const std::vector<OperationID> &frontline = {}); // return operations ahead of the given frontline
};

// ===== RichText implementation =====

template <typename FormatProvider>
size_t RichText<FormatProvider>::size() const
{
	return doc.size();
}

template <typename FormatProvider>
bool RichText<FormatProvider>::empty() const
{
	return doc.size() == 0;
}

template <typename FormatProvider>
FormatProvider &RichText<FormatProvider>::formatProvider()
{
	return doc.formatProvider();
}

template <typename FormatProvider>
std::string RichText<FormatProvider>::toString() const
{
	return doc.toString();
}

template <typename FormatProvider>
std::string RichText<FormatProvider>::slice(size_t begin, size_t end) const
{
	std::string s = doc.toString();
	if (begin >= s.size())
		return "";
	if (end > s.size())
		end = s.size();
	return s.substr(begin, end - begin);
}

template <typename FormatProvider>
std::string RichText<FormatProvider>::slice(const Anchor &begin, const Anchor &end) const
{
	size_t b = toPos(begin);
	size_t e = toPos(end);
	if (b > e)
		return "";
	return slice(b, e);
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::insert(size_t pos, const std::string &text)
{
	Anchor anchor_val = doc.insertAnchor(pos);
	return insert(anchor_val, text);
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::insert(const Anchor &anchor, const std::string &text)
{
	Insertion op(doc.id(), doc.stamp(), anchor, text);
	if (!doc.insert(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::del(size_t begin, size_t end)
{
	return del(toClosedRange(begin, end));
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::del(const ClosedRange &range)
{
	Deletion op(doc.id(), doc.stamp(), range.begin, range.end);
	if (!doc.del(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename FormatProvider>
template <typename RangeType, typename T>
size_t RichText<FormatProvider>::format(const std::string &style_name, size_t begin, size_t end, const T &value)
{
	if constexpr (std::is_same_v<RangeType, ClosedRange>)
	{
		return format(style_name, toClosedRange(begin, end), value);
	}
	else if constexpr (std::is_same_v<RangeType, OpenedRange>)
	{
		return format(style_name, toOpenedRange(begin, end), value);
	}
	else
	{
		static_assert(false, "Unsupported RangeType");
		return 0;
	}
}

template <typename FormatProvider>
template <typename RangeType, typename T>
size_t RichText<FormatProvider>::format(const std::string &style_name, RangeType range, const T &value)
{
	Formatting<RangeType, T> op(doc.id(), doc.stamp(), style_name, range, value);
	if (!doc.format(op))
		return 0; // invalid style or no-op
	undo_stack.push(op.stamp);
	return op.stamp;
}
template <typename FormatProvider>
template <typename T>
T RichText<FormatProvider>::style(const std::string &style_name, size_t pos) const
{
	return doc.template style<T>(doc.find(pos), style_name);
}

template <typename FormatProvider>
OpenedRange RichText<FormatProvider>::toOpenedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.reversedAnchor(end);
	return OpenedRange(anchorBegin, anchorEnd);
}

template <typename FormatProvider>
ClosedRange RichText<FormatProvider>::toClosedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.anchor(end);
	return ClosedRange(anchorBegin, anchorEnd);
}

template <typename FormatProvider>
Anchor RichText<FormatProvider>::toAnchor(size_t pos) const
{
	return doc.insertAnchor(pos);
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::toPos(const Anchor &anchor) const
{
	return doc.pos(anchor);
}

template <typename FormatProvider>
void RichText<FormatProvider>::beginGroup()
{
	undo_stack.push(kGroupStart);
}

template <typename FormatProvider>
void RichText<FormatProvider>::endGroup()
{
	undo_stack.push(kGroupEnd);
}

template <typename FormatProvider>
bool RichText<FormatProvider>::canUndo() const
{
	return !undo_stack.empty();
}

template <typename FormatProvider>
bool RichText<FormatProvider>::canRedo() const
{
	return !redo_stack.empty();
}

template <typename FormatProvider>
void RichText<FormatProvider>::undo()
{
	if (undo_stack.empty())
		return;

	uint32_t target = undo_stack.top();
	undo_stack.pop();

	if (target == kGroupEnd)
	{
		redo_stack.push(kGroupEnd);
		int balance = 1;
		while (balance > 0 && !undo_stack.empty())
		{
			uint32_t item = undo_stack.top();
			undo_stack.pop();

			if (item == kGroupEnd)
			{
				balance++;
				redo_stack.push(item);
			}
			else if (item == kGroupStart)
			{
				balance--;
				redo_stack.push(item);
			}
			else
			{
				UndoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), item});
				doc.undo(op);
				redo_stack.push(item);
			}
		}
	}
	else if (target == kGroupStart)
	{
		// Just move start marker to redo stack
		redo_stack.push(target);
	}
	else
	{
		UndoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), target});
		if (doc.undo(op))
			redo_stack.push(target);
	}
}

template <typename FormatProvider>
void RichText<FormatProvider>::redo()
{
	if (redo_stack.empty())
		return;

	uint32_t target = redo_stack.top();
	redo_stack.pop();

	if (target == kGroupStart)
	{
		undo_stack.push(kGroupStart);
		int balance = 1;
		while (balance > 0 && !redo_stack.empty())
		{
			uint32_t item = redo_stack.top();
			redo_stack.pop();

			if (item == kGroupStart)
			{
				balance++;
				undo_stack.push(item);
			}
			else if (item == kGroupEnd)
			{
				balance--;
				undo_stack.push(item);
			}
			else
			{
				RedoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), item});
				doc.redo(op);
				undo_stack.push(item);
			}
		}
	}
	else if (target == kGroupEnd)
	{
		undo_stack.push(target);
	}
	else
	{
		RedoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), target});
		if (doc.redo(op))
			undo_stack.push(target);
	}
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::undoSpecific(OperationID opID)
{
	UndoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.undo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename FormatProvider>
size_t RichText<FormatProvider>::redoSpecific(OperationID opID)
{
	RedoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.redo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename FormatProvider>
ReplicaID RichText<FormatProvider>::replicaID() const
{
	return doc.id();
}

template <typename FormatProvider>
ReplicaID RichText<FormatProvider>::origin() const
{
	return doc.origin();
}

template <typename FormatProvider>
bool RichText<FormatProvider>::apply(const Operation &op)
{
	return doc.apply(op);
}

template <typename FormatProvider>
void RichText<FormatProvider>::apply(const std::vector<std::unique_ptr<Operation>> &ops)
{
	doc.apply(ops);
}

template <typename FormatProvider>
std::vector<OperationID> RichText<FormatProvider>::frontline()
{
	return doc.frontline();
}

template <typename FormatProvider>
std::vector<std::unique_ptr<Operation>> RichText<FormatProvider>::diff(const std::vector<OperationID> &frontline)
{
	return doc.diff(frontline);
}
