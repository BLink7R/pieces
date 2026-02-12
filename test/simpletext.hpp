#pragma once

#include <algorithm>
#include <iostream>
#include <string>
#include <unordered_map>
#include <type_traits>
#include <vector>

#include "piecetree.hpp"

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
class TextFormatProvider
{
private:
	struct StyleInfo
	{
		int key;
		std::string name;
		RangeTree<4> tree;
		int last_int_value{0};
		bool has_int_value{false};
	};

	static std::vector<StyleInfo> &styles()
	{
		static std::vector<StyleInfo> s;
		return s;
	}

	static std::unordered_map<std::string, int> &name_to_key()
	{
		static std::unordered_map<std::string, int> m;
		return m;
	}

public:
	TextFormatProvider() = default;

	// Explicitly register a style name and get its numeric key.
	static int addStyle(const std::string &name)
	{
		auto &m = name_to_key();
		auto &vec = styles();
		auto it = m.find(name);
		if (it != m.end())
			return it->second;
		int key = static_cast<int>(vec.size());
		vec.push_back(StyleInfo{key, name, RangeTree<4>{}, 0, false});
		m.emplace(name, key);
		return key;
	}

	// For tests: read back the last int value applied to a style.
	static bool getIntStyleValue(const std::string &name, int &out)
	{
		auto &m = name_to_key();
		auto &vec = styles();
		auto it = m.find(name);
		if (it == m.end())
			return false;
		const StyleInfo &info = vec[static_cast<std::size_t>(it->second)];
		if (!info.has_int_value)
			return false;
		out = info.last_int_value;
		return true;
	}

	// Called by PieceCRDT::format to map style name to integer key.
	template <typename RangeType, typename T>
	int styleKey(const Formatting<RangeType, T> &op)
	{
		auto &m = name_to_key();
		auto &vec = styles();
		int key;
		auto it = m.find(op.key);
		if (it == m.end())
		{
			key = static_cast<int>(vec.size());
			vec.push_back(StyleInfo{key, op.key, RangeTree<4>{}, 0, false});
			m.emplace(op.key, key);
		}
		else
		{
			key = it->second;
		}

		if constexpr (std::is_same_v<T, int>)
		{
			StyleInfo &info = vec[static_cast<std::size_t>(key)];
			info.last_int_value = op.value;
			info.has_int_value = true;
		}

		return key;
	}

	// Access the RangeTree for a given style key.
	RangeTree<4> &style(int key)
	{
		auto &vec = styles();
		return vec[static_cast<std::size_t>(key)].tree;
	}

	const RangeTree<4> &style(int key) const
	{
		auto &vec = styles();
		return vec[static_cast<std::size_t>(key)].tree;
	}
};

class PieceCRDTValidator : public PieceCRDT<void>
{
public:
	ClosedRange historyRange(size_t start, size_t end)
	{
		Iterator it_begin = piece_tree.findHistory(start);
		Iterator it_end = piece_tree.findHistory(end);
		Anchor anchor_begin;
		if (!it_begin.isNull())
		{ // begin is reversed
			Segment *seg = it_begin->seg;
			anchor_begin = Anchor(seg->operationID(), start - it_begin.position().total + it_begin->seg_pos - seg->len);
		}
		Anchor anchor_end;
		if (!it_end.isNull())
		{
			if (end - it_end.position().total + it_end->seg_pos == 0)
			{
				--end;
			}
			Segment *seg = it_end->seg;
			anchor_end = Anchor(seg->operationID(), end - it_end.position().total + it_end->seg_pos);
		}
		return ClosedRange(anchor_begin, anchor_end);
	}

	bool validate()
	{
		std::string total_str;
		std::vector<int> delete_count;
		size_t total_size = (--end()).position().total;
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
		std::cout << "PieceCRDTValidator: content " << (valid ? "matches" : "differs")
				  << ", expected size " << reconstructed.size()
				  << ", actual size " << size() << "\n";
		// std::cout << "PieceCRDTValidator: expect content \"" << reconstructed << "\"\n";
		// std::cout << "PieceCRDTValidator: actual content \"" << toString() << "\"\n";
		return valid;
	}
};