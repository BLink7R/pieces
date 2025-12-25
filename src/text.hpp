#pragma once

#include <string>
#include <utility>

#include "piecetree.hpp"

using FrontLine = std::vector<std::pair<ReplicaID, uint32_t>>;

struct Range
{
	Anchor begin;
	Anchor end;
};

class PlainText
{
public:
	class Iterator
	{
	};

	PlainText();

	// basic information
	size_t size() const;
	size_t byteSize() const;
	size_t rowSize() const;
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
	Iterator find(size_t row, size_t column);

	// edit, return operation stamp
	size_t insert(size_t pos, const std::string &text);
	size_t insert(size_t row, size_t column, const std::string &text);
	size_t insert(const Anchor &anchor, const std::string &text);
	size_t del(size_t begin, size_t end);
	size_t del(size_t row_begin, size_t column_begin, size_t row_end, size_t column_end);
	size_t del(const Anchor &begin, const Anchor &end);

	// index conversion
	Anchor toAnchor(size_t pos) const;
	Anchor toAnchor(size_t row, size_t column) const;
	size_t toOffset(size_t row, size_t column) const;
	size_t toOffset(const Anchor &anchor) const;

	// undo/redo, only undo/redo local user's operations
	bool canUndo() const;
	bool canRedo() const;
	void undo();
	void redo();

	// remote operations
	ReplicaID replicaID() const;
	void apply(const Operation &op);
	void apply(const std::vector<Operation> &ops);
	FrontLine frontline();
	std::vector<Operation> diff(const FrontLine &frontline = FrontLine()); // return operations ahead of the given frontline
};
