#pragma once

#include "textcrdt.hpp"

// Rich text CRDT: plain text core (TextCRDT) + format support.
// FormatProvider is a compile-time policy providing:
//   - styleKey(name) -> int                       (name -> key, -1 if unknown)
//   - style(key) -> RangeTree<4>&                 (per-style range tag tree)
//   - getDefaultValue<T>(name) -> T               (default style value)
//   - apply(Doc*, const Operation&) -> bool       (dispatch a remote RangeFormat op)
template <typename FormatProvider, typename CharT = char>
class RichTextCRDT : public TextCRDT<CharT>
{
private:
	using Base = TextCRDT<CharT>;
	FormatProvider format_provider;

	void redoFormat(StoredRangeOp *target)
	{
		assert(target->left->status == TagStatus::Undone && target->right->status == TagStatus::Undone);
		int style_key = target->styleType();
		auto left_piece = Base::piece_tree.find(target->left->anchor);
		auto right_piece = Base::piece_tree.find(target->right->anchor);

		// Update tag->old for left and right boundary pieces by checking first and last pieces
		// inside the deletion. We do not check pieces outside the deletion range because it
		// needs to process the right closed anchor case.
		{
			auto piece_before = left_piece;
			target->left->old.setBad();
			auto op = piece_before->styles[style_key];
			assert(op == nullptr || op->right->old.isGood());
			if (op == nullptr)
				target->left->old = nullptr;
			else if (op->left->anchor != target->left->anchor)
			{
				if (*op < *target)
					target->left->old = op;
			}
			else if (op->left->old == nullptr || *op->left->old < *target)
			{
				assert(op->left->status == TagStatus::Active && "tombStone should be Active");
				target->left->old = op->left->old;
			}
		}
		{
			auto piece_after = right_piece;
			target->right->old.setBad();
			auto op = piece_after->styles[style_key];
			assert(op == nullptr || op->left->old.isGood());
			if (op == nullptr)
				target->right->old = nullptr;
			else if (op->right->anchor != target->right->anchor)
			{
				if (*op < *target)
					target->right->old = op;
			}
			else if (op->right->old == nullptr || *op->right->old < *target)
			{
				assert(op->right->status == TagStatus::Active && "tombStone should be Active");
				target->right->old = op->right->old;
			}
		}

		std::multimap<void *, Piece<CharT> *> cache;
		Base::redoRangeOp(target, [style_key, &cache](Piece<CharT> *piece, StoredRangeOp *op)
		{
			if (piece->styles[style_key] == nullptr || *piece->styles[style_key] < *op)
				cache.insert({piece->styles.raw(), piece});
		});
		for (auto it = cache.begin(); it != cache.end();)
		{
			auto range = cache.equal_range(it->first);
			Piece<CharT> *first_piece = range.first->second;
			Formats new_formats = first_piece->styles;
			new_formats.set(style_key, target);
			for (auto jt = range.first; jt != range.second; ++jt)
			{
				jt->second->styles = new_formats;
			}
			it = range.second;
		}
	}

	void undoFormat(StoredRangeOp *target)
	{
		int style_key = target->styleType();

		std::multimap<std::pair<void *, StoredRangeOp *>, Piece<CharT> *> cache;
		auto ops_covered = Base::undoRangeOp(target, [target, style_key, &cache](Piece<CharT> *piece, StoredRangeOp *newest)
		{
			if (piece->styles[style_key] == target)
				cache.insert({{piece->styles.raw(), newest}, piece});
		});
		for (auto it = cache.begin(); it != cache.end();)
		{
			auto range = cache.equal_range(it->first);
			StoredRangeOp *newest_op = it->first.second;
			Piece<CharT> *first_piece = range.first->second;
			Formats new_formats = first_piece->styles;
			new_formats.set(style_key, newest_op);
			for (auto jt = range.first; jt != range.second; ++jt)
			{
				Piece<CharT> *piece = jt->second;
				piece->styles = new_formats;
			}
			it = range.second;
		}

		for (auto op : ops_covered)
		{
			std::multimap<void *, Piece<CharT> *> cache;
			Base::redoRangeOp(op, [style_key, &cache](Piece<CharT> *piece, StoredRangeOp *op)
			{
				if (piece->styles[style_key] == nullptr || *piece->styles[style_key] < *op)
					cache.insert({piece->styles.raw(), piece});
			});
			for (auto it = cache.begin(); it != cache.end();)
			{
				auto range = cache.equal_range(it->first);
				Piece<CharT> *first_piece = range.first->second;
				Formats new_formats = first_piece->styles;
				new_formats.set(style_key, op);
				for (auto jt = range.first; jt != range.second; ++jt)
				{
					jt->second->styles = new_formats;
				}
				it = range.second;
			}
		}
	}

protected:
	bool applyFormatOp(const Operation &op) override
	{
		return format_provider.apply(this, op);
	}

	void redoRangeFormat(StoredRangeOp *op) override
	{
		redoFormat(op);
	}

	void undoRangeFormat(StoredRangeOp *op) override
	{
		undoFormat(op);
	}

	bool applyObjectInsertOp(const Operation &op) override
	{
		switch (op.type)
		{
		case OperationType::ParagraphHead:
			return insertParagraphHead(static_cast<const ParagraphHeadInsert &>(op));
		case OperationType::InlineObject:
			return insertInlineObject(static_cast<const InlineObjectInsert &>(op));
		default:
			return false;
		}
	}

public:
	using typename Base::Iterator;
	using typename Base::String;

	RichTextCRDT(const ReplicaID &origin = {})
		: Base(origin)
	{
	}

	auto &formatProvider()
	{
		return format_provider;
	}

	// insert an atomic size-1 paragraph head (describes paragraph attributes)
	bool insertParagraphHead(const ParagraphHeadInsert &op)
	{
		return insertObjectImpl<StoredParagraphHead<CharT>>(op.replica, op.stamp, op.anchor);
	}

	// insert an atomic size-1 inline object (image/shape/table/...)
	bool insertInlineObject(const InlineObjectInsert &op)
	{
		if (op.id.empty())
			return false; // no-op
		return insertObjectImpl<StoredInlineObject<CharT>>(op.replica, op.stamp, op.anchor, op.id);
	}

	template <typename T>
	T style(typename Base::Iterator it, std::string style_name) const
	{
		int style_key = format_provider.styleKey(style_name);
		if (style_key < 0)
			return T{};
		StoredRangeOp *op = it->styles[style_key];
		if (op == nullptr)
			return format_provider.template getDefaultValue<T>(style_name);
		return static_cast<StoredFormat<T> *>(op)->value;
	}

	template <typename RangeType, typename T>
	bool format(const Formatting<RangeType, T> &op)
	{
		int style_key = format_provider.styleKey(op.key);
		if (style_key < 0)
			return false; // invalid style
		if (op.range.begin == op.range.end)
			return false; // no-op
		auto begin = Base::toStored(op.range.begin);
		auto end = Base::toStored(op.range.end);
		if (begin.seg == nullptr || end.seg == nullptr)
			return false; // invalid anchor

		StoredFormat<T> *stored_op = Base::template storeOp<StoredFormat<T>>(op.replica, op.stamp, style_key, op.value);
		if (!stored_op)
			return false; // duplicate operation

		auto style_tree = format_provider.style(stored_op->key);
		auto [left_it, right_it] = style_tree.apply(
			RangeTag(true, begin, stored_op), RangeTag(false, end, stored_op), Base::piece_tree);
		stored_op->left = &*left_it;
		stored_op->right = &*right_it;

		redoFormat(stored_op);
		return true;
	}

private:
	template <typename T, typename... Args>
	bool insertObjectImpl(const ReplicaID &replica, uint32_t stamp, const Anchor &anchor, Args &&...args)
	{
		auto stored_anchor = Base::toStored(anchor);
		if (stored_anchor.seg == nullptr)
			return false; // invalid anchor

		T *segment = Base::template storeOp<T>(replica, stamp, std::forward<Args>(args)...);
		if (segment == nullptr)
			return false; // duplicate operation

		segment->anchor = stored_anchor;
		Base::piece_tree.insert(segment);
		return true;
	}
};
