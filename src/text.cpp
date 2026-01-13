#include "text.hpp"

size_t PlainText::size() const { return doc.size(); }

bool PlainText::empty() const { return doc.size() == 0; }

std::string PlainText::toString() const { return doc.toString(); }

std::string PlainText::slice(size_t begin, size_t end) const
{
	std::string s = doc.toString();
	if (begin >= s.size())
		return "";
	if (end > s.size())
		end = s.size();
	return s.substr(begin, end - begin);
}

std::string PlainText::slice(const Anchor &begin, const Anchor &end) const
{
	size_t b = toPos(begin);
	size_t e = toPos(end);
	if (b > e)
		return "";
	return slice(b, e);
}

size_t PlainText::insert(size_t pos, const std::string &text)
{
	Anchor anchor = toAnchor(pos);
	return insert(anchor, text);
}

size_t PlainText::insert(const Anchor &anchor, const std::string &text)
{
	Insertion op(doc.id(), doc.stamp(), anchor, text);
	if (!doc.insert(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

size_t PlainText::del(size_t begin, size_t end)
{
	Anchor anchorBegin = toAnchor(begin);
	Anchor anchorEnd = toAnchor(end);
	return del(anchorBegin, anchorEnd);
}

size_t PlainText::del(const Anchor &begin, const Anchor &end)
{
	Deletion op(doc.id(), doc.stamp(), begin, end);
	if (!doc.del(op))
		return 0;
	undo_stack.push(op.stamp);
	return op.stamp;
}

Anchor PlainText::toAnchor(size_t pos) const
{
	return doc.anchor(pos);
}

size_t PlainText::toPos(const Anchor &anchor) const
{
	return doc.pos(anchor);
}

bool PlainText::canUndo() const { return !undo_stack.empty(); }

bool PlainText::canRedo() const { return !redo_stack.empty(); }

void PlainText::undo()
{
	if (undo_stack.empty())
		return;
	uint32_t target = undo_stack.top();
	undo_stack.pop();

	UndoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), target});
	doc.undo(op);
	redo_stack.push(target);
}

void PlainText::redo()
{
	if (redo_stack.empty())
		return;
	uint32_t target = redo_stack.top();
	redo_stack.pop();

	RedoOperation op(doc.id(), doc.stamp(), OperationID{doc.id(), target});
	doc.redo(op);
	undo_stack.push(target);
}

size_t PlainText::undoSpecific(OperationID opID)
{
	UndoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.undo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

size_t PlainText::redoSpecific(OperationID opID)
{
	RedoOperation op(doc.id(), doc.stamp(), opID);
	if (!doc.redo(op))
		return 0;
	undo_stack.push(opID.stamp);
	return op.stamp;
}

ReplicaID PlainText::replicaID() const { return doc.id(); }

ReplicaID PlainText::origin() const { return doc.origin(); }

bool PlainText::apply(const Operation &op)
{
	return doc.apply(op);
}

void PlainText::apply(const std::vector<std::unique_ptr<Operation>> &ops)
{
	doc.apply(ops);
}

std::vector<OperationID> PlainText::frontline()
{
	return doc.frontline();
}

std::vector<std::unique_ptr<Operation>> PlainText::diff(const std::vector<OperationID> &frontline)
{
	return doc.diff(frontline);
}
