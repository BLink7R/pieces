#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "piecetree.hpp"
#include "rangetree.hpp"
#include "textcrdt.hpp"

class SimpleText
{
public:
	void insert(size_t pos, const std::string &text)
	{
		if (pos > buffer.size())
			pos = buffer.size();
		buffer.insert(pos, text);
	}

	void erase(size_t pos, size_t length)
	{
		if (pos >= buffer.size())
			return;
		if (pos + length > buffer.size())
			length = buffer.size() - pos;
		buffer.erase(pos, length);
	}

	std::string toString() const
	{
		return buffer;
	}

	size_t size() const
	{
		return buffer.size();
	}

	void clear()
	{
		buffer.clear();
	}

private:
	std::string buffer;
};

class SimpleDeferredText
{
public:
	enum class OpType
	{
		Insert,
		Delete,
		Undo,
		Redo,
	};

	struct Operation
	{
		size_t id;
		OpType type;
		size_t pos;
		std::string text; // for insert
		size_t length;	  // for delete
		size_t target_id; // for undo/redo
		bool valid = true;
	};

private:
	std::vector<Operation> ops;
	size_t next_id = 1;

	Operation *get_op(size_t id)
	{
		return &ops[id - 1];
	}

public:
	size_t insert(size_t pos, const std::string &text)
	{
		size_t id = next_id++;
		ops.push_back({id, OpType::Insert, pos, text, 0, 0, true});
		return id;
	}

	size_t erase(size_t pos, size_t length)
	{
		size_t id = next_id++;
		ops.push_back({id, OpType::Delete, pos, "", length, 0, true});
		return id;
	}

	size_t undo(size_t id)
	{
		Operation *op = get_op(id);
		if (!op)
			return 0;

		size_t target_id = id;
		OpType action = OpType::Undo;
		bool target_valid = false;

		if (op->type == OpType::Undo)
		{
			target_id = op->target_id;
			action = OpType::Redo;
			target_valid = true;
		}
		else if (op->type == OpType::Redo)
		{
			target_id = op->target_id;
			action = OpType::Undo;
			target_valid = false;
		}

		size_t new_id = next_id++;
		ops.push_back({new_id, action, 0, "", 0, target_id, true});

		Operation *target_op = get_op(target_id);
		if (target_op)
		{
			target_op->valid = target_valid;
		}
		return new_id;
	}

	size_t redo(size_t id)
	{
		Operation *op = get_op(id);
		if (!op)
			return 0;

		size_t target_id = id;
		OpType action = OpType::Redo;
		bool target_valid = true;

		if (op->type == OpType::Undo)
		{
			target_id = op->target_id;
			action = OpType::Undo;
			target_valid = false;
		}
		else if (op->type == OpType::Redo)
		{
			target_id = op->target_id;
			action = OpType::Redo;
			target_valid = true;
		}

		size_t new_id = next_id++;
		ops.push_back({new_id, action, 0, "", 0, target_id, true});

		Operation *target_op = get_op(target_id);
		if (target_op)
		{
			target_op->valid = target_valid;
		}
		return new_id;
	}

	std::string toString() const
	{
		std::string res;
		for (const auto &op : ops)
		{
			if (!op.valid)
				continue;

			if (op.type == OpType::Insert)
			{
				if (op.pos > res.length())
				{
					res += op.text;
				}
				else
				{
					res.insert(op.pos, op.text);
				}
			}
			else if (op.type == OpType::Delete)
			{
				if (op.pos < res.length())
				{
					size_t count = std::min(op.length, res.length() - op.pos);
					res.erase(op.pos, count);
				}
			}
		}
		return res;
	}

	size_t size() const
	{
		return toString().length();
	}

	void clear()
	{
		ops.clear();
		next_id = 1;
	}
};

// A simple format provider for tests, mapping style name -> integer key
// and recording the last int value applied for each style.
class TestFormatProvider
{
private:
	struct StyleInfo
	{
		int key;
		std::string name;
		int default_value;
		RangeTree<4> tree{};
	};
	std::vector<StyleInfo> styles;
	std::unordered_map<std::string, int> name_to_key;

public:
	TestFormatProvider() = default;

	// Explicitly register a style name and get its numeric key.
	int addStyle(const std::string &name, int default_value = 0)
	{
		auto it = name_to_key.find(name);
		if (it != name_to_key.end())
			return it->second;
		int key = static_cast<int>(styles.size());
		styles.push_back(StyleInfo{key, name, default_value, RangeTree<4>{}});
		name_to_key.emplace(name, key);
		return key;
	}

	template <typename T>
	T getDefaultValue(const std::string &style_name) const
	{
		auto it = name_to_key.find(style_name);
		if (it == name_to_key.end())
			return T{};
		int key = it->second;
		return styles[static_cast<std::size_t>(key)].default_value;
	}

	// Called by PieceCRDT::format to map style name to integer key.
	int styleKey(const std::string &style_name) const
	{
		auto it = name_to_key.find(style_name);
		if (it == name_to_key.end())
			return -1;
		return it->second;
	}

	RangeTree<4> &style(int key)
	{
		return styles[static_cast<std::size_t>(key)].tree;
	}

	template <typename Doc>
	bool apply(Doc *doc, const Operation &op)
	{
		if (op.type != OperationType::RangeFormat)
			return false;
		const auto &fmt_op = static_cast<const Formatting<OpenedRange, int> &>(op);
		return doc->format(fmt_op);
	}
};

class PieceCRDTValidator : public PieceCRDT<void>
{
public:
	// closed range for history index
	ClosedRange historyRange(size_t start, size_t end)
	{
		Iterator it_begin = piece_tree.findHistory(start);
		Iterator it_end = piece_tree.findHistory(end);
		Anchor anchor_begin;
		if (!it_begin.isNull())
		{ // begin is reversed
			StoredContent *seg = it_begin->seg;
			anchor_begin = Anchor(seg->operationID(), static_cast<int32_t>(static_cast<int64_t>(start) - it_begin.position().total + it_begin->seg_pos - seg->len));
		}
		Anchor anchor_end;
		if (!it_end.isNull())
		{
			if (end - it_end.position().total + it_end->seg_pos == 0)
			{
				--end;
			}
			StoredContent *seg = it_end->seg;
			anchor_end = Anchor(seg->operationID(), static_cast<int32_t>(static_cast<int64_t>(end) - it_end.position().total + it_end->seg_pos));
		}
		return ClosedRange(anchor_begin, anchor_end);
	}

	bool validate()
	{
		std::string total_str;
		std::vector<int> delete_count;
		auto end_it = end();
		--end_it;
		size_t total_size = end_it.position().total;
		total_str.reserve(total_size);
		delete_count.resize(total_size, false);
		for (auto it = this->begin(), end_it = this->end(); it != end_it; ++it)
		{
			total_str.append(it->data, it->len);
		}

		//
		// for (const auto& tag: deletions)
		// {
		// 	std::cout << piece_tree.historyPos(tag.anchor) << " id " << tag.cur->replica << " pos " << tag.anchor.pos << "\n";
		// }
		for (const auto &replica : replicas)
		{
			for (const auto &op : replica.operations)
			{
				if (op && op->type() == OperationType::Delete)
				{
					auto *del = static_cast<StoredDeletion *>(op.get());
					// 如果删除操作已被撤销，则不计入
					if (del->hasUndo())
						continue;

					auto &left = del->left->anchor;
					auto &right = del->right->anchor;

					size_t start = piece_tree.historyPos(del->left->anchor);
					size_t end = piece_tree.historyPos(del->right->anchor);

					for (size_t k = start; k < end; ++k)
					{
						delete_count[k]++;
					}
				}
			}
		}

		std::string reconstructed;
		reconstructed.reserve(total_size);
		for (size_t i = 0; i < total_size; ++i)
		{
			if (delete_count[i] == 0)
			{
				reconstructed += total_str[i];
			}
		}
		bool valid = reconstructed == toString();
		// std::cout << "PieceCRDTValidator: content " << (valid ? "matches" : "differs")
		// 		  << ", expected size " << reconstructed.size()
		// 		  << ", actual size " << size() << "\n";
		// std::cout << "PieceCRDTValidator: expect content \"" << reconstructed << "\"\n";
		// std::cout << "PieceCRDTValidator: actual content \"" << toString() << "\"\n";
		return valid;
	}
};