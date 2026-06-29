#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <utf8cpp/utf8.h>
#include <vector>

#include "crdt.hpp"
#include "gb+tree.hpp"
#include "storedop.hpp"

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