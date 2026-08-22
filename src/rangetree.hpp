#pragma once

#include "piecetree.hpp"

template <uint8_t N>
class RangeTree : public OrderedSet<RangeTag, N>
{
public:
	using Base = OrderedSet<RangeTag, N>;
	using Iterator = typename Base::Iterator;
	using NodeT = typename Base::NodeT;
	using InternalNodeT = typename Base::InternalNodeT;
	using LeafNodeT = typename Base::LeafNodeT;

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
		int32_t offset = tag.anchor.segOffset() - static_cast<int32_t>(piece_it->seg_offset);
		if (offset == piece_it->len)
			++piece_it;
		else if (offset > 0)
			piece_it = piece_tree.split(piece_it, offset);

		size_t history_offset = piece_it.position().total;

		auto it = this->insert(std::move(tag),
							   [&piece_tree, history_offset](const RangeTag &a, const RangeTag &b)
		{
			size_t a_offset = piece_tree.historyOffset(a.anchor);
			if (a_offset != history_offset)
				return a_offset < history_offset;
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