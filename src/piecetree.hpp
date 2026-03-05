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

	auto pieceAt(int32_t position) const;
	auto insertPiece(Piece *piece);

	auto insertSegment(Segment *segment)
	{ // TODO: we can change it to std::find if segment is small
		auto it = std::lower_bound(
			child.begin(), child.end(), segment,
			[](const StoredContent *a, const StoredContent *b)
		{
			if (a->anchor.segPos() != b->anchor.segPos())
				return a->anchor.segPos() < b->anchor.segPos();
			if (a->anchor.pos != b->anchor.pos) // reversed vs normal, normal goes first
				return a->anchor.pos > b->anchor.pos;
			if (a->anchor.pos > 0)
				return *a < *b; // normal: earlier goes first
			else
				return *b < *a; // reversed: later goes first
		});
		return child.insert(it, segment);
	}

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
	Segment *seg{nullptr};
	const char *data{nullptr};
	int32_t len{0};
	int32_t seg_pos{0};
	StoredRangeOp *tombStone{nullptr};
	Formats styles;

	Piece() = default;
	Piece(Segment *seg)
		: seg(seg),
		  data(seg->data.get()),
		  len(seg->len),
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
		return data < other.data;
	}
};

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
		Segment *seg = anchor.seg;
		auto piece_it = seg->pieceAt(anchor.pos);
		if (piece_it == seg->split_piece.end())
			return this->end();
		return Iterator(*piece_it);
	}

	size_t historyPos(const StoredAnchor &anchor) const
	{
		Iterator it = find(anchor);
		return it.position().total + (anchor.segPos() - it->seg_pos);
	}

	Iterator insert(Segment *segment)
	{
		StoredAnchor anchor = segment->anchor;
		Segment *parent = anchor.seg;
		assert(parent != nullptr);
		auto piece_it = find(anchor);
		assert(piece_it != this->end());
		int32_t pos = static_cast<int32_t>(anchor.segPos() - piece_it->seg_pos);
		auto conflict_it = parent->insertSegment(segment);
		Piece new_node(segment);
		// handle insertion ambiguity
		if (pos == 0)
		{ // for reversed anchor
			Piece *piece = &*piece_it;
			if (conflict_it + 1 != parent->child.end())
			{
				Segment *after = *(conflict_it + 1);
				if (after->anchor.pos == anchor.pos) // has conflict
				{
					for (; !after->child.empty(); after = after->child[0])
					{
						if (after->child[0]->anchor.pos != 0)
							break;
					}
					piece = after->split_piece[0];
				}
			}
			piece_it = this->insertBefore(Iterator(piece), new_node);
		}
		else if (pos == piece_it->len)
		{ // for normal anchor
			Piece *piece = &*piece_it;
			if (conflict_it != parent->child.begin())
			{
				Segment *before = *(conflict_it - 1);
				if (before->anchor.pos == anchor.pos) // has conflict
				{
					for (; !before->child.empty(); before = before->child.back())
					{
						if (before->child.back()->anchor.pos != before->len)
							break;
					}
					piece = before->split_piece.back();
				}
			}
			piece_it = this->insertAfter(Iterator(piece), new_node);
		}
		else
		{
			piece_it = split(piece_it, pos);
			piece_it = this->insertBefore(piece_it, new_node);
		}
		segment->insertPiece(&*piece_it);
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
		it->seg->insertPiece(&*new_it);
		return new_it;
	}
};

template <uint8_t N>
class RangeTree : public OrderedSet<RangeTag, N>
{
public:
	using Base = OrderedSet<RangeTag, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	RangeTree() = default;
	~RangeTree() = default;

	// should ganrantee left.anchor < right.anchor
	template <typename PieceTree>
	auto apply(RangeTag left, RangeTag right, PieceTree &piece_tree)
	{
		// left and right can be on the same piece, so we need to split right first
		auto begin = this->addTag<true>(left, piece_tree);
		auto end = this->addTag<false>(right, piece_tree);
		return std::make_pair(begin, end);
	}

protected:
	template <bool IsLeft, typename PieceTree>
	auto addTag(RangeTag tag, PieceTree &piece_tree)
	{
		auto piece_it = piece_tree.find(tag.anchor);
		int32_t pos = tag.anchor.segPos() - piece_it->seg_pos;
		if (pos == piece_it->len)
			++piece_it;
		else if (pos > 0)
			piece_it = piece_tree.split(piece_it, pos);

		size_t history_pos = piece_it.position().total;

		auto it = this->insert(std::move(tag),
							   [&piece_tree, history_pos](const RangeTag &a, const RangeTag &b)
		{
			size_t a_pos = piece_tree.historyPos(a.anchor);
			if (a_pos != history_pos)
				return a_pos < history_pos;
			// new right tag-----  -----new left tag
			// old right tag--- |  | ---old left tag
			//  (prev piece]  | |  | |  [next piece)
			// -------------------------- covered old range op
			if (a.is_left != b.is_left)
				return b.is_left;
			else if (a.is_left)
				return *b.cur < *a.cur;
			else
				return *a.cur < *b.cur;
		});
		return it;
	}
};