#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utf8cpp/utf8.h>
#include <vector>

#include "crdt.hpp"
#include "format.hpp"
#include "gb+tree.hpp"
#include "storedop.hpp"

struct Piece;

// Text is stored in segments. Whenever text is inserted, a new segment is created,
// and the target segment with the insertion offset is stored, keeping the target unchanged.
// As user edits are usually small, and we need to implement anchor in 2 directions, we
// limit the length of segments to INT_MAX. The length of pieces can be much smaller.
struct Segment : public StoredContent
{
	mutable std::vector<Piece *> split_piece;
	std::unique_ptr<const char[]> data{nullptr};
	std::unique_ptr<size_t[]> line_breaks{nullptr};
	size_t line_break_count{0};

	Segment(const std::string &str)
		: StoredContent()
	{
		// TODO: ensure that str.size() <= INT32_MAX
		data = std::make_unique<const char[]>(str.size() + 1);
		memcpy(const_cast<char *>(data.get()), str.c_str(), str.size() + 1);
		len = static_cast<int32_t>(utf8::distance(data.get(), data.get() + str.size()));

		// collect newline positions in UTF-8 character offsets
		if (!str.empty())
		{
			std::vector<size_t> breaks;
			const char *it = str.data();
			const char *end = it + str.size();
			size_t char_index = 0;
			while (it < end)
			{
				if (*it == '\r')
				{
					if ((it + 1) < end && *(it + 1) == '\n')
						breaks.push_back(char_index + 1);
					else
						breaks.push_back(char_index);
				}
				else if (*it == '\n')
					breaks.push_back(char_index + 1);
				utf8::next(it, end);
			}
			line_break_count = breaks.size();
			if (line_break_count != 0)
			{
				line_breaks = std::make_unique<size_t[]>(line_break_count);
				for (size_t i = 0; i < line_break_count; ++i)
					line_breaks[i] = breaks[i];
			}
		}
	}
	~Segment() override = default;

	OperationType type() const override
	{
		return OperationType::Insert;
	}

	const char *rawData() const override
	{
		return data.get();
	}

	// if anchor is normal, return the piece before the position
	// if anchor is reversed, return the piece after the position
	auto pieceAt(int32_t position) const;
	auto insertPiece(Piece *piece);

	size_t findLineBreak(size_t utf8_offset) const
	{
		if (line_break_count == 0)
			return 0;
		const size_t *begin = line_breaks.get();
		const size_t *end = begin + line_break_count;
		const size_t *it = std::lower_bound(begin, end, utf8_offset);
		return static_cast<size_t>(it - begin);
	}

	Segment(Segment &&other) noexcept = default;
	Segment &operator=(Segment &&other) noexcept = default;
	Segment(const Segment &other) = delete;
	Segment &operator=(const Segment &other) = delete;
};

struct PlaceHolder : public StoredContent
{
	PlaceHolder() = default;
};

struct PieceInfo
{
	size_t total{0};
	size_t visible{0};

	PieceInfo operator+(const PieceInfo &other) const
	{
		return {.total = total + other.total, .visible = visible + other.visible};
	}
	PieceInfo &operator+=(const PieceInfo &other)
	{
		visible += other.visible;
		total += other.total;
		return *this;
	}
	PieceInfo &operator-=(const PieceInfo &other)
	{
		visible -= other.visible;
		total -= other.total;
		return *this;
	}
	bool operator!=(const PieceInfo &other) const
	{
		return visible != other.visible || total != other.total;
	}
};

// Segments are split into pieces according to global offsets.
// TODO: limit the length of piece, as the split operation is O(n) in the length of piece.
struct Piece
{
	StoredContent *seg{nullptr};
	const char *data{nullptr};
	uint32_t len{0};
	uint32_t seg_pos{0};
	StoredRangeOp *tombStone{nullptr};
	Formats styles;

	Piece() = default;
	Piece(StoredContent *seg)
		: seg(seg),
		  data(seg->rawData()),
		  len(static_cast<uint32_t>(seg->len)),
		  seg_pos(0) {}

	bool isRemoved() const
	{
		return tombStone != nullptr;
	}

	PieceInfo size() const
	{
		return {.total = static_cast<size_t>(len),
				.visible = isRemoved() ? 0 : static_cast<size_t>(len)};
	}

	bool operator<(const Piece &other) const
	{
		return seg < other.seg;
	}
};

// placeholder char serialized for inline objects.
// currently a single byte (0xFF) so the char-based len == byte-based serialization holds;
// multi-byte serialization is handled separately later.
inline const char *objectPlaceholder()
{
	static const char placeholder[] = "\xFF";
	return placeholder;
}

// inline objects (image/shape/table) are atomic len-1 contents: never split, so a single
// piece pointer is enough. The payload is referenced by an external id and is not
// replicated inside the text CRDT.
struct StoredObject : public StoredContent
{
	std::string id;	// reference to the external object data
	Piece *piece{nullptr}; // the single piece derived from this object

	StoredObject(std::string id)
		: StoredContent(), id(std::move(id))
	{
		len = 1;
	}

	bool isObject() const override
	{
		return true;
	}

	const char *rawData() const override
	{
		return objectPlaceholder();
	}
};

// the first/last piece derived from a content (single piece for inline objects)
inline Piece *firstPiece(StoredContent *content)
{
	if (content->isObject())
		return static_cast<StoredObject *>(content)->piece;
	auto &pieces = static_cast<Segment *>(content)->split_piece;
	return pieces.empty() ? nullptr : pieces.front();
}

inline Piece *lastPiece(StoredContent *content)
{
	if (content->isObject())
		return static_cast<StoredObject *>(content)->piece;
	auto &pieces = static_cast<Segment *>(content)->split_piece;
	return pieces.empty() ? nullptr : pieces.back();
}

// if anchor is normal, return the piece before the position
// if anchor is reversed, return the piece after the position
inline auto Segment::pieceAt(int32_t pos) const
{
	if (pos == 0 || std::abs(pos) > len)
		return split_piece.end();
	if (pos > 0)
		return std::lower_bound(
			split_piece.begin(), split_piece.end(), pos,
			[](const Piece *p, size_t position)
		{
			return p->seg_pos + p->len < position;
		});
	return std::lower_bound(
		split_piece.begin(), split_piece.end(), len + pos,
		[](const Piece *p, size_t position)
	{
		return p->seg_pos + p->len <= position;
	});
}

inline auto Segment::insertPiece(Piece *piece)
{
	auto it = std::lower_bound(
		split_piece.begin(), split_piece.end(), piece,
		[](const Piece *a, const Piece *b)
	{
		return a->seg_pos < b->seg_pos;
	});
	return split_piece.insert(it, piece);
}

template <uint8_t N>
class PieceTree : public Sequence<PieceInfo, Piece, N>
{
public:
	using Base = Sequence<PieceInfo, Piece, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	PieceTree(Segment *initial_segment)
	{
		auto it = this->insertBefore(this->end(), Piece(initial_segment));
		initial_segment->split_piece.push_back(&*it);
	}

	Iterator findHistory(size_t history_pos) const
	{
		return Base::find(history_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.total;
		});
	}

	// Finds the first piece with end position > file_pos
	Iterator upper_bound(size_t file_pos) const
	{
		return Base::find(file_pos, [](size_t a, const PieceInfo &b)
		{
			return a < b.visible;
		});
	}

	// Finds the first piece with end position >= file_pos
	Iterator lower_bound(size_t file_pos) const
	{
		return Base::find(file_pos, [](size_t a, const PieceInfo &b)
		{
			return a <= b.visible;
		});
	}

	// if anchor is normal, return the piece before the position
	// if anchor is reversed, return the piece after the position
	Iterator find(const StoredAnchor &anchor) const
	{
		StoredContent *seg = anchor.seg;
		Piece *piece = nullptr;
		if (seg->isObject())
		{ // atomic object, exactly one piece
			piece = static_cast<StoredObject *>(seg)->piece;
		}
		else
		{
			auto *text_seg = static_cast<Segment *>(seg);
			auto piece_it = text_seg->pieceAt(anchor.pos);
			if (piece_it == text_seg->split_piece.end())
				return this->end();
			piece = *piece_it;
		}
		return piece ? Iterator(piece) : this->end();
	}

	size_t historyPos(const StoredAnchor &anchor) const
	{
		Iterator it = find(anchor);
		return it.position().total + (anchor.segPos() - it->seg_pos);
	}

	Iterator insert(StoredContent *segment)
	{
		StoredAnchor anchor = segment->anchor;
		StoredContent *parent = anchor.seg;
		assert(parent != nullptr);
		auto piece_it = find(anchor);
		assert(piece_it != this->end());
		int32_t pos = static_cast<int32_t>(anchor.segPos() - piece_it->seg_pos);
		auto conflict_it = parent->insertContent(segment);
		Piece new_node(segment);
		// handle insertion ambiguity
		if (pos == 0)
		{ // for reversed anchor
			Piece *piece = &*piece_it;
			if (conflict_it + 1 != parent->child.end())
			{
				StoredContent *after = *(conflict_it + 1);
				if (after->anchor.pos == anchor.pos) // has conflict
				{
					for (; !after->child.empty(); after = after->child[0])
					{
						if (after->child[0]->anchor.pos != 0)
							break;
					}
					piece = firstPiece(after);
				}
			}
			piece_it = this->insertBefore(Iterator(piece), new_node);
		}
		else if (pos == piece_it->len)
		{ // for normal anchor
			Piece *piece = &*piece_it;
			if (conflict_it != parent->child.begin())
			{
				StoredContent *before = *(conflict_it - 1);
				if (before->anchor.pos == anchor.pos) // has conflict
				{
					for (; !before->child.empty(); before = before->child.back())
					{
						if (before->child.back()->anchor.pos != before->len)
							break;
					}
					piece = lastPiece(before);
				}
			}
			piece_it = this->insertAfter(Iterator(piece), new_node);
		}
		else
		{
			piece_it = split(piece_it, pos);
			piece_it = this->insertBefore(piece_it, new_node);
		}
		if (segment->isObject())
			static_cast<StoredObject *>(segment)->piece = &*piece_it;
		else
			static_cast<Segment *>(segment)->insertPiece(&*piece_it);
		return piece_it;
	}

	// return the right part
	Iterator split(Iterator it, int32_t pos)
	{
		assert(0 < pos && pos < it->len);

		size_t offset = 0;
		const char *ptr = it->data;
		utf8::advance(ptr, pos, ptr + 4 * it->len); // max 4 bytes per utf8 char
		offset = ptr - it->data;

		// new node is the right part
		Piece new_node = *it;
		it->len = pos;
		new_node.data += offset;
		new_node.seg_pos += pos;
		new_node.len -= pos;
		it.key() = it->size(); // no need to update(), insertBefore() will do it
		this->update(it, it);

		auto new_it = this->insertAfter(it, new_node);
		static_cast<Segment *>(it->seg)->insertPiece(&*new_it); // split only applies to text pieces
		return new_it;
	}
};