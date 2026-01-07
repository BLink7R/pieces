#pragma once

#include <string>
#include <utility>

#include <stack>
#include <vector>

#include "piecetree.hpp"

class PlainText
{
private:
	PieceCRDT doc;
	std::stack<uint32_t> undo_stack;
	std::stack<uint32_t> redo_stack;

public:
	using Iterator = PieceCRDT::Iterator;

	PlainText();

	// basic information
	size_t size() const;
	// size_t rowSize() const;
	bool empty() const;

	// export
	std::string toString() const;
	std::string slice(size_t begin, size_t end) const;
	std::string slice(const Anchor &begin, const Anchor &end) const;

	// query, iterate
	Iterator begin();
	Iterator end();
	Iterator find(size_t pos);
	Iterator find(const Anchor &anchor);
	// Iterator find(size_t row, size_t column);

	// edit, return operation stamp
	size_t insert(size_t pos, const std::string &text);
	// size_t insert(size_t row, size_t column, const std::string &text);
	size_t insert(const Anchor &anchor, const std::string &text);
	size_t del(size_t begin, size_t end);
	// size_t del(size_t row_begin, size_t column_begin, size_t row_end, size_t column_end);
	size_t del(const Anchor &begin, const Anchor &end);

	// index conversion
	Anchor toAnchor(size_t pos) const;
	// Anchor toAnchor(size_t row, size_t column) const;
	// size_t toPos(size_t row, size_t column) const;
	size_t toPos(const Anchor &anchor) const;

	// undo/redo, only undo/redo local user's operations
	// the operation created WONT be recorded in undo/redo stack
	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();

	// undo/redo specific operation, other user's operations included
	// the operation created WILL be recorded in undo/redo stack
	size_t undoSpecific(OperationID opID);
	size_t redoSpecific(OperationID opID);

	// remote operations
	ReplicaID replicaID() const;
	void apply(const Operation &op);
	void apply(const std::vector<Operation> &ops);
	std::vector<OperationID> frontline();
	std::vector<Operation> diff(const std::vector<OperationID> &frontline = {}); // return operations ahead of the given frontline
};
