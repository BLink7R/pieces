#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

#include "crdt.hpp"
#include "format.hpp"
#include "gb+tree.hpp"
#include "storedop.hpp"

template <typename CharT>
struct Piece;

// Text is stored in segments. Whenever text is inserted, a new segment is created,
// and the target segment with the insertion offset is stored, keeping the target unchanged.
// As user edits are usually small, and we need to implement anchor in 2 directions, we
// limit the length of segments to INT_MAX. The length of pieces can be much smaller.
// CharT is the underlying storage unit: char (UTF-8 bytes) or char16_t (UTF-16 code units).
template <typename CharT>
struct Segment : public StoredContent
{
	using String = std::basic_string<CharT>;

	mutable std::vector<Piece<CharT> *> split_piece;
	std::unique_ptr<const CharT[]> data{nullptr};
	std::unique_ptr<size_t[]> line_breaks{nullptr};
	size_t line_break_count{0};

	Segment(const String &str)
		: StoredContent()
	{
		// TODO: ensure that str.size() <= INT32_MAX
		data = std::make_unique<const CharT[]>(str.size() + 1);
		memcpy(const_cast<CharT *>(data.get()), str.data(), (str.size() + 1) * sizeof(CharT));
		len = static_cast<int32_t>(str.size());

		// collect newline offsets in CharT offsets
		if (!str.empty())
		{
			std::vector<size_t> breaks;
			for (size_t i = 0; i < str.size(); ++i)
			{
				if (str[i] == CharT('\r'))
				{
					if ((i + 1) < str.size() && str[i + 1] == CharT('\n'))
						breaks.push_back(i + 1);
					else
						breaks.push_back(i);
				}
				else if (str[i] == CharT('\n'))
					breaks.push_back(i + 1);
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

	// if anchor is normal, return the piece before the offset
	// if anchor is reversed, return the piece after the offset
	auto pieceAt(int32_t offset) const;
	auto insertPiece(Piece<CharT> *piece);

	size_t findLineBreak(size_t offset) const
	{
		if (line_break_count == 0)
			return 0;
		const size_t *begin = line_breaks.get();
		const size_t *end = begin + line_break_count;
		const size_t *it = std::lower_bound(begin, end, offset);
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
template <typename CharT>
inline const CharT *objectPlaceholder();

template <typename CharT>
struct Piece
{
	StoredContent *seg{nullptr};
	const CharT *data{nullptr};
	uint32_t len{0};
	uint32_t seg_pos{0};
	StoredRangeOp *tombStone{nullptr};
	Formats styles;

	Piece() = default;
	Piece(StoredContent *seg)
		: seg(seg),
		  data(seg->isObject() ? objectPlaceholder<CharT>() : static_cast<Segment<CharT> *>(seg)->data.get()),
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
// char: single byte 0xFF; char16_t: U+FFFC OBJECT REPLACEMENT CHARACTER.
template <typename CharT>
inline const CharT *objectPlaceholder()
{
	if constexpr (std::is_same_v<CharT, char>)
	{
		static const char placeholder[] = "\xFF";
		return placeholder;
	}
	else
	{
		static const char16_t placeholder[] = u"\xFFFC";
		return placeholder;
	}
}

// inline objects (image/shape/table) are atomic len-1 contents: never split, so a single
// piece pointer is enough. The payload is referenced by an external id and is not
// replicated inside the text CRDT.
template <typename CharT>
struct StoredObject : public StoredContent
{
	std::string id;			   // reference to the external object data
	Piece<CharT> *piece{nullptr}; // the single piece derived from this object

	StoredObject(std::string id)
		: StoredContent(), id(std::move(id))
	{
		len = 1;
	}

	bool isObject() const override
	{
		return true;
	}
};

// the first/last piece derived from a content (single piece for inline objects)
template <typename CharT>
inline Piece<CharT> *firstPiece(StoredContent *content)
{
	if (content->isObject())
		return static_cast<StoredObject<CharT> *>(content)->piece;
	auto &pieces = static_cast<Segment<CharT> *>(content)->split_piece;
	return pieces.empty() ? nullptr : pieces.front();
}

template <typename CharT>
inline Piece<CharT> *lastPiece(StoredContent *content)
{
	if (content->isObject())
		return static_cast<StoredObject<CharT> *>(content)->piece;
	auto &pieces = static_cast<Segment<CharT> *>(content)->split_piece;
	return pieces.empty() ? nullptr : pieces.back();
}

// if anchor is normal, return the piece before the position
// if anchor is reversed, return the piece after the position
template <typename CharT>
inline auto Segment<CharT>::pieceAt(int32_t offset) const
{
	if (offset == 0 || std::abs(offset) > len)
		return split_piece.end();
	if (offset > 0)
		return std::lower_bound(
			split_piece.begin(), split_piece.end(), offset,
			[](const Piece<CharT> *p, size_t position)
		{
			return p->seg_pos + p->len < position;
		});
	return std::lower_bound(
		split_piece.begin(), split_piece.end(), len + offset,
		[](const Piece<CharT> *p, size_t position)
	{
		return p->seg_pos + p->len <= position;
	});
}

template <typename CharT>
inline auto Segment<CharT>::insertPiece(Piece<CharT> *piece)
{
	auto it = std::lower_bound(
		split_piece.begin(), split_piece.end(), piece,
		[](const Piece<CharT> *a, const Piece<CharT> *b)
	{
		return a->seg_pos < b->seg_pos;
	});
	return split_piece.insert(it, piece);
}

template <uint8_t N, typename CharT = char>
class PieceTree : public Sequence<PieceInfo, Piece<CharT>, N>
{
public:
	using Base = Sequence<PieceInfo, Piece<CharT>, N>;
	using Iterator = typename Base::Iterator;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

	PieceTree(Segment<CharT> *initial_segment)
	{
		auto it = this->insertBefore(this->end(), Piece<CharT>(initial_segment));
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
		Piece<CharT> *piece = nullptr;
		if (seg->isObject())
		{ // atomic object, exactly one piece
			piece = static_cast<StoredObject<CharT> *>(seg)->piece;
		}
		else
		{
			auto *text_seg = static_cast<Segment<CharT> *>(seg);
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
		Piece<CharT> new_node(segment);
		// handle insertion ambiguity
		if (pos == 0)
		{ // for reversed anchor
			Piece<CharT> *piece = &*piece_it;
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
					piece = firstPiece<CharT>(after);
				}
			}
			piece_it = this->insertBefore(Iterator(piece), new_node);
		}
		else if (pos == piece_it->len)
		{ // for normal anchor
			Piece<CharT> *piece = &*piece_it;
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
					piece = lastPiece<CharT>(before);
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
			static_cast<StoredObject<CharT> *>(segment)->piece = &*piece_it;
		else
			static_cast<Segment<CharT> *>(segment)->insertPiece(&*piece_it);
		return piece_it;
	}

	// return the right part
	Iterator split(Iterator it, int32_t pos)
	{
		assert(0 < pos && pos < it->len);

		// new node is the right part; CharT is fixed width so pos is both the
		// character and byte-unit offset within the piece
		Piece<CharT> new_node = *it;
		it->len = pos;
		new_node.data += pos;
		new_node.seg_pos += pos;
		new_node.len -= pos;
		it.key() = it->size(); // no need to update(), insertBefore() will do it
		this->update(it, it);

		auto new_it = this->insertAfter(it, new_node);
		static_cast<Segment<CharT> *>(it->seg)->insertPiece(&*new_it); // split only applies to text pieces
		return new_it;
	}
};