#include "text.hpp"
#include "textcrdt.hpp"

namespace {
	constexpr uint32_t kGroupStart = std::numeric_limits<uint32_t>::max();
	constexpr uint32_t kGroupEnd = std::numeric_limits<uint32_t>::max() - 1;
}

template <typename CharT>
size_t PlainText<CharT>::size() const { return doc.size(); }

template <typename CharT>
bool PlainText<CharT>::empty() const { return doc.size() == 0; }

template <typename CharT>
typename PlainText<CharT>::String PlainText<CharT>::toString() const { return doc.toString(); }

template <typename CharT>
typename PlainText<CharT>::String PlainText<CharT>::slice(size_t begin, size_t end) const
{
	using String = typename PlainText<CharT>::String;
	String s = doc.toString();
	if (begin >= s.size())
		return String();
	if (end > s.size())
		end = s.size();
	return s.substr(begin, end - begin);
}

template <typename CharT>
typename PlainText<CharT>::String PlainText<CharT>::slice(const Anchor &begin, const Anchor &end) const
{
	size_t b = toOffset(begin);
	size_t e = toOffset(end);
	if (b > e)
		return String();
	return slice(b, e);
}

template <typename CharT>
size_t PlainText<CharT>::insert(size_t offset, const String &text)
{
	Anchor anchor = doc.insertAnchor(offset);
	return insert(anchor, text);
}

template <typename CharT>
size_t PlainText<CharT>::insert(const Anchor &anchor, const String &text)
{
	Insertion<CharT> op(doc.id(), doc.stamp(), anchor, text);
	if (!doc.insert(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename CharT>
size_t PlainText<CharT>::del(size_t begin, size_t end)
{
	return del(toClosedRange(begin, end));
}

template <typename CharT>
size_t PlainText<CharT>::del(const ClosedRange &range)
{
	Deletion op(doc.id(), doc.stamp(), range.begin, range.end);
	if (!doc.del(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

template <typename CharT>
OpenedRange PlainText<CharT>::toOpenedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.reversedAnchor(end);
	return OpenedRange(anchorBegin, anchorEnd);
}

template <typename CharT>
ClosedRange PlainText<CharT>::toClosedRange(size_t begin, size_t end) const
{
	Anchor anchorBegin = doc.reversedAnchor(begin);
	Anchor anchorEnd = doc.anchor(end);
	return ClosedRange(anchorBegin, anchorEnd);
}

template <typename CharT>
Anchor PlainText<CharT>::toAnchor(size_t offset) const
{
	return doc.insertAnchor(offset);
}

template <typename CharT>
size_t PlainText<CharT>::toOffset(const Anchor &anchor) const
{
	return doc.offset(anchor);
}

template <typename CharT>
void PlainText<CharT>::beginGroup()
{
	undo_stack.push(kGroupStart);
}

template <typename CharT>
void PlainText<CharT>::endGroup()
{
	undo_stack.push(kGroupEnd);
}

template <typename CharT>
bool PlainText<CharT>::canUndo() const { return !undo_stack.empty(); }

template <typename CharT>
bool PlainText<CharT>::canRedo() const { return !redo_stack.empty(); }

template <typename CharT>
void PlainText<CharT>::undo()
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
		// Should generally not be the top-level operation unless undoing an open group or mismatched group.
		// Behave safely by just moving it to redo.
		redo_stack.push(target);
	}
	else
	{
		UndoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), target});
		doc.undo(op);
		redo_stack.push(target);
	}
}

template <typename CharT>
void PlainText<CharT>::redo()
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
		doc.redo(op);
		undo_stack.push(target);
	}
}

template <typename CharT>
size_t PlainText<CharT>::undoSpecific(OperationID opID)
{
	UndoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.undo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename CharT>
size_t PlainText<CharT>::redoSpecific(OperationID opID)
{
	RedoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.redo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

template <typename CharT>
ReplicaID PlainText<CharT>::replicaID() const { return doc.id(); }

template <typename CharT>
ReplicaID PlainText<CharT>::origin() const { return doc.origin(); }

template <typename CharT>
bool PlainText<CharT>::apply(const Operation &op)
{
	return doc.apply(op);
}

template <typename CharT>
void PlainText<CharT>::apply(const std::vector<std::unique_ptr<Operation>> &ops)
{
	doc.apply(ops);
}

template <typename CharT>
std::vector<OperationID> PlainText<CharT>::frontline()
{
	return doc.frontline();
}

template <typename CharT>
std::vector<std::unique_ptr<Operation>> PlainText<CharT>::diff(const std::vector<OperationID> &frontline)
{
	return doc.diff(frontline);
}

template class PlainText<char>;
template class PlainText<char16_t>;
