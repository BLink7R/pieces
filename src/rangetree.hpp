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