#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "piecetree.hpp"

class SimpleText
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

class DocumentValidator : public Document
{
public:
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
		for (const auto &replica : replicas)
		{
			for (const auto &op : replica.segments)
			{
				if (op && op->type == OperationType::Delete)
				{
					auto *del = static_cast<StoredDeletion *>(op.get());
					// 如果删除操作已被撤销，则不计入
					if (del->has_undo)
						continue;

					auto &left = del->left->anchor;
					auto &right = del->right->anchor;

					size_t start = piece_tree.find(left).position().total;
					size_t end = piece_tree.find(right).position().total;

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
		std::cout << "DocumentValidator: content " << (valid ? "matches" : "differs")
				  << ", expected size " << reconstructed.size()
				  << ", actual size " << size() << "\n";
		// std::cout << "DocumentValidator: expect content \"" << reconstructed << "\"\n";
		// std::cout << "DocumentValidator: actual content \"" << toString() << "\"\n";
		return valid;
	}
};