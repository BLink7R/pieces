#pragma once

#include <string>
#include <utility>

#include <limits>
#include <stack>
#include <vector>

#include "textcrdt.hpp"

// TODO: check index is valid
template <typename CharT = char>
class PlainText
{
private:
	PieceCRDT<void, CharT> doc;
	std::stack<uint32_t> undo_stack;
	std::stack<uint32_t> redo_stack;

public:
	using Iterator = PieceCRDT<void, CharT>::Iterator;
	using String = std::basic_string<CharT>;

	PlainText() = default;
	PlainText(const ReplicaID &rid)
		: doc(rid) {}

	// basic information
	size_t size() const;
	// size_t rowSize() const;
	bool empty() const;

	// export
	String toString() const;
	String slice(size_t begin, size_t end) const;
	String slice(const Anchor &begin, const Anchor &end) const;

	// iterate
	// Iterator begin();
	// Iterator end();
	// Iterator find(size_t pos);
	// Iterator find(const Anchor &anchor);
	// Iterator find(size_t row, size_t column);

	// edit, return operation stamp
	size_t insert(size_t pos, const String &text);
	// size_t insert(size_t row, size_t column, const String &text);
	size_t insert(const Anchor &anchor, const String &text);
	size_t insertObject(size_t pos, const std::string &object_id);
	size_t insertObject(const Anchor &anchor, const std::string &object_id);
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

template <typename FormatProvider, typename CharT = char>
class RichText
{
private:
	PieceCRDT<FormatProvider, CharT> doc;
	std::stack<uint32_t> undo_stack;
	std::stack<uint32_t> redo_stack;

	static constexpr uint32_t kGroupStart = std::numeric_limits<uint32_t>::max();
	static constexpr uint32_t kGroupEnd = std::numeric_limits<uint32_t>::max() - 1;

public:
	using Iterator = PieceCRDT<FormatProvider, CharT>::Iterator;
	using String = std::basic_string<CharT>;

	RichText() = default;
	RichText(const ReplicaID &rid)
		: doc(rid) {}

	// basic information
	size_t size() const;
	bool empty() const;
	FormatProvider &formatProvider();

	// export
	String toString() const;
	String slice(size_t begin, size_t end) const;
	String slice(const Anchor &begin, const Anchor &end) const;

	// iterate
	// Iterator begin();
	// Iterator end();
	// Iterator find(size_t pos);
	// Iterator find(const Anchor &anchor);

	// edit, return operation stamp
	size_t insert(size_t pos, const String &text);
	size_t insert(const Anchor &anchor, const String &text);
	size_t insertObject(size_t pos, const std::string &object_id);
	size_t insertObject(const Anchor &anchor, const std::string &object_id);
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

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::size() const
{
	return doc.size();
}

template <typename FormatProvider, typename CharT>
bool RichText<FormatProvider, CharT>::empty() const
{
	return doc.size() == 0;
}

template <typename FormatProvider, typename CharT>
FormatProvider &RichText<FormatProvider, CharT>::formatProvider()
{
	return doc.formatProvider();
}

template <typename FormatProvider, typename CharT>
typename RichText<FormatProvider, CharT>::String RichText<FormatProvider, CharT>::toString() const
{
	return doc.toString();
}

template <typename FormatProvider, typename CharT>
typename RichText<FormatProvider, CharT>::String RichText<FormatProvider, CharT>::slice(size_t begin, size_t end) const
{
	String s = doc.toString();
	if (begin >= s.size())
		return String();
	if (end > s.size())
		end = s.size();
	return s.substr(begin, end - begin);
}

template <typename FormatProvider, typename CharT>
typename RichText<FormatProvider, CharT>::String RichText<FormatProvider, CharT>::slice(const Anchor &begin, const Anchor &end) const
{
	size_t b = toPos(begin);
	size_t e = toPos(end);
	if (b > e)
		return String();
	return slice(b, e);
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::insert(size_t pos, const String &text)
{
	Anchor anchor_val = doc.insertAnchor(pos);
	return insert(anchor_val, text);
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::insert(const Anchor &anchor, const String &text)
{
	Insertion<CharT> op(doc.id(), doc.stamp(), anchor, text);
	if (!doc.insert(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::insertObject(size_t pos, const std::string &object_id)
{
	Anchor anchor_val = doc.insertAnchor(pos);
	return insertObject(anchor_val, object_id);
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::insertObject(const Anchor &anchor, const std::string &object_id)
{
	Insertion<CharT> op(doc.id(), doc.stamp(), anchor, String(), object_id);
	if (!doc.insert(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::del(size_t begin, size_t end)
{
	return del(toClosedRange(begin, end));
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::del(const ClosedRange &range)
{
	Deletion op(doc.id(), doc.stamp(), range.begin, range.end);
	if (!doc.del(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename FormatProvider, typename CharT>
template <typename RangeType, typename T>
size_t RichText<FormatProvider, CharT>::format(const std::string &style_name, size_t begin, size_t end, const T &value)
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

template <typename FormatProvider, typename CharT>
template <typename RangeType, typename T>
size_t RichText<FormatProvider, CharT>::format(const std::string &style_name, RangeType range, const T &value)
{
	Formatting<RangeType, T> op(doc.id(), doc.stamp(), style_name, range, value);
	if (!doc.format(op))
		return 0; // invalid style or no-op
	undo_stack.push(op.stamp);
	return op.stamp;
}
template <typename FormatProvider, typename CharT>
template <typename T>
T RichText<FormatProvider, CharT>::style(const std::string &style_name, size_t pos) const
{
	return doc.template style<T>(doc.find(pos), style_name);
}

template <typename FormatProvider, typename CharT>
OpenedRange RichText<FormatProvider, CharT>::toOpenedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.reversedAnchor(end);
	return OpenedRange(anchorBegin, anchorEnd);
}

template <typename FormatProvider, typename CharT>
ClosedRange RichText<FormatProvider, CharT>::toClosedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.anchor(end);
	return ClosedRange(anchorBegin, anchorEnd);
}

template <typename FormatProvider, typename CharT>
Anchor RichText<FormatProvider, CharT>::toAnchor(size_t pos) const
{
	return doc.insertAnchor(pos);
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::toPos(const Anchor &anchor) const
{
	return doc.pos(anchor);
}

template <typename FormatProvider, typename CharT>
void RichText<FormatProvider, CharT>::beginGroup()
{
	undo_stack.push(kGroupStart);
}

template <typename FormatProvider, typename CharT>
void RichText<FormatProvider, CharT>::endGroup()
{
	undo_stack.push(kGroupEnd);
}

template <typename FormatProvider, typename CharT>
bool RichText<FormatProvider, CharT>::canUndo() const
{
	return !undo_stack.empty();
}

template <typename FormatProvider, typename CharT>
bool RichText<FormatProvider, CharT>::canRedo() const
{
	return !redo_stack.empty();
}

template <typename FormatProvider, typename CharT>
void RichText<FormatProvider, CharT>::undo()
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

template <typename FormatProvider, typename CharT>
void RichText<FormatProvider, CharT>::redo()
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

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::undoSpecific(OperationID opID)
{
	UndoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.undo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename FormatProvider, typename CharT>
size_t RichText<FormatProvider, CharT>::redoSpecific(OperationID opID)
{
	RedoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.redo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename FormatProvider, typename CharT>
ReplicaID RichText<FormatProvider, CharT>::replicaID() const
{
	return doc.id();
}

template <typename FormatProvider, typename CharT>
ReplicaID RichText<FormatProvider, CharT>::origin() const
{
	return doc.origin();
}

template <typename FormatProvider, typename CharT>
bool RichText<FormatProvider, CharT>::apply(const Operation &op)
{
	return doc.apply(op);
}

template <typename FormatProvider, typename CharT>
void RichText<FormatProvider, CharT>::apply(const std::vector<std::unique_ptr<Operation>> &ops)
{
	doc.apply(ops);
}

template <typename FormatProvider, typename CharT>
std::vector<OperationID> RichText<FormatProvider, CharT>::frontline()
{
	return doc.frontline();
}

template <typename FormatProvider, typename CharT>
std::vector<std::unique_ptr<Operation>> RichText<FormatProvider, CharT>::diff(const std::vector<OperationID> &frontline)
{
	return doc.diff(frontline);
}
