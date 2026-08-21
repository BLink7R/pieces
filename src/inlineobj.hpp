#pragma once

#include <string>
#include <utility>
#include <vector>

// Inline contents that occupy a single character position in the text stream.
// Two kinds:
//   - ParagraphHead: describes the attributes of a paragraph, placed at the start
//     of the paragraph it annotates
//   - InlineObject: a general inline object (image/shape/table/...)
//
// The CRDT storage layer only stores the object id (see StoredObject in
// piecetree.hpp); the actual data model is managed out-of-band by the application
// (registration and query by id are separated from insertion into the document).
class InlineObj
{
public:
	virtual ~InlineObj() = default;

	virtual bool isParagraphHead() const = 0;
	virtual const std::string &id() const = 0;
};

// a general inline object (image/shape/table/...), referenced by an external id
class InlineObject : public InlineObj
{
private:
	std::string id_;

public:
	std::string type;								   // e.g. "image", "shape", "table"
	std::vector<std::pair<std::string, std::string>> attrs; // optional custom attributes

	explicit InlineObject(std::string id)
		: id_(std::move(id)) {}

	bool isParagraphHead() const override
	{
		return false;
	}
	const std::string &id() const override
	{
		return id_;
	}
};

// paragraph head: describes paragraph attributes, occupying one character position
// at the beginning of the paragraph it annotates.
// a paragraph head has no external object id; its CRDT insertion is identified
// solely by its anchor position.
class ParagraphHead : public InlineObj
{
public:
	std::vector<std::pair<std::string, std::string>> attrs; // e.g. heading level, alignment, list

	ParagraphHead() = default;

	bool isParagraphHead() const override
	{
		return true;
	}
	const std::string &id() const override
	{
		static const std::string empty;
		return empty;
	}
};
