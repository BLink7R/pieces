#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#include "taggedptr.hpp"

template <typename K, uint8_t N>
struct InternalNode;

template <typename K, uint8_t N>
struct Node
{
	const bool is_leaf;
	uint8_t index{0}; // index in parent's children array
	uint8_t count{0}; // number of keys
	InternalNode<K, N> *parent{nullptr};
	std::array<K, N> keys;

	Node(bool leaf = false) : is_leaf(leaf) {}
};

template <typename K, uint8_t N>
struct InternalNode : public Node<K, N>
{
	std::array<Node<K, N> *, N> subs;

	InternalNode() : Node<K, N>(false) {}

	void set(uint8_t index, const K &key, Node<K, N> *child)
	{
		this->keys[index] = key;
		subs[index] = child;
		if (child)
		{
			child->index = index;
			child->parent = this;
		}
	}

	void move(uint8_t index1, uint8_t index2)
	{
		set(index2, this->keys[index1], subs[index1]);
	}

	void move(uint8_t index1, InternalNode *other, uint8_t index2)
	{
		other->set(index2, this->keys[index1], subs[index1]);
	}
};

template <typename L>
struct BaseIter
{
	L *node{nullptr};
	uint8_t index{0};

	BaseIter(L *node = nullptr, uint8_t index = 0)
		: node(node), index(index) {}
};

template <typename L>
struct SentinelNode
{
	L *node{nullptr};
	uint8_t index{0};

	SentinelNode(L *node = nullptr, uint8_t index = 0)
		: node(node), index(index) {}
};

// a grow only b+tree
// find method is provided by derived classes
template <typename K, typename Leaf, uint8_t N, typename Summarizer>
	requires std::is_base_of_v<Node<K, 2 * N - 1>, Leaf>
class BPlusTree
{
protected:
	static constexpr uint8_t ORDER = 2 * N - 1;
	using Node = Node<K, ORDER>;
	using InternalNode = InternalNode<K, ORDER>;
	using LeafNode = Leaf;
	using BaseIter = BaseIter<LeafNode>;

	Node *root{nullptr};
	LeafNode *first{nullptr};
	LeafNode *last{nullptr};
	size_t sz{0};

public:
	BPlusTree()
	{
		root = first = last = new LeafNode();
		auto sentinel = new SentinelNode<LeafNode>(last, 0);
		last->next = sentinel;
	}
	~BPlusTree() {}

	size_t size() const { return sz; }

protected:
	template <typename... Args>
	BaseIter insertLeaf(LeafNode *leaf, uint8_t index, Args &&...args)
	{
		++sz;
		if (leaf->count < ORDER)
		{
			insertNode(leaf, index, std::forward<Args>(args)...);
			return BaseIter(leaf, index);
		}
		else
		{
			LeafNode *new_leaf = splitNode(leaf, index, std::forward<Args>(args)...);
			if (leaf->next.isSpecial())
			{
				last = new_leaf;
				auto sentinel = leaf->next.asSpecial();
				sentinel->node = new_leaf;
			}
			else
				leaf->next->prev = new_leaf;
			new_leaf->next = leaf->next;
			new_leaf->prev = leaf;
			leaf->next = new_leaf;
			return index < N ? BaseIter(leaf, index) : BaseIter(new_leaf, index - N);
		}
	}

private:
	void insertInternal(InternalNode *node, uint8_t index, const K &key, Node *child)
	{
		if (node->count < ORDER)
			insertNode(node, index, key, child);
		else
			splitNode(node, index, key, child);
	}

	template <typename NodeType, typename... Args>
	void insertNode(NodeType *node, uint8_t index, Args &&...args)
	{
		assert(node->count < ORDER);

		for (int i = node->count; i > index; --i)
			node->move(i - 1, i);
		node->set(index, std::forward<Args>(args)...);
		++node->count;

		for (Node *current = node; current->parent; current = current->parent)
		{
			K new_key = Summarizer()(current->keys.data(), current->count);
			if (new_key != current->parent->keys[current->index])
				current->parent->keys[current->index] = new_key;
			else
				break;
		}
	}

	template <typename NodeType, typename... Args>
	NodeType *splitNode(NodeType *node, uint8_t index, Args &&...args)
	{
		assert(node->count == ORDER);

		NodeType *new_node = new NodeType();
		if (index < N)
		{
			for (int i = ORDER; i >= N; --i)
				node->move(i - 1, new_node, i - N);
			for (int i = N - 1; i > index; --i)
				node->move(i - 1, i);
			node->set(index, std::forward<Args>(args)...);
		}
		else
		{
			for (int i = ORDER; i > index; --i)
				node->move(i - 1, new_node, i - N);
			for (int i = index - 1; i >= N; --i)
				node->move(i, new_node, i - N);
			new_node->set(index - N, std::forward<Args>(args)...);
		}

		node->count = new_node->count = N;
		if (node->parent)
		{
			node->parent->keys[node->index] = Summarizer()(node->keys.data(), node->count);
			insertInternal(node->parent, node->index + 1, Summarizer()(new_node->keys.data(), new_node->count), new_node);
		}
		else
		{
			InternalNode *new_root = new InternalNode();
			new_root->set(0, Summarizer()(node->keys.data(), node->count), node);
			new_root->set(1, Summarizer()(new_node->keys.data(), new_node->count), new_node);
			new_root->count = 2;
			root = new_root;
		}
		return new_node;
	}
};

// leaf node types when we want to get iterators from value ptrs
template <typename V, typename L>
struct PinnedCell : public BaseIter<L>
{
	mutable V value{};

	PinnedCell() = default;
	PinnedCell(V val)
		: value(std::move(val)) {}

	static PinnedCell *cellOf(V *val)
	{
		// assert((size_t)(&cell->value) - (size_t)cell.raw() == sizeof(BaseIter<L>));
		return reinterpret_cast<PinnedCell *>((size_t)val - sizeof(BaseIter<L>));
	}
};

template <typename V, typename L>
struct PinnedIter
{
	using Cell = typename L::Cell;
	using CellPtr = TaggedPtr<Cell, SentinelNode<L>>;
	CellPtr cell{nullptr};

	PinnedIter(L *node = nullptr, uint8_t index = 0)
		: cell(node->get(index)) {}
	PinnedIter(const Cell *cell = nullptr)
		: cell(cell) {}
	PinnedIter(V *val)
		: cell(Cell::cellOf(val)) {}
	PinnedIter(const SentinelNode<L> *ptr)
		: cell(ptr) {}

	L *leaf()
	{
		return cell->node;
	}
	V *operator->()
	{
		return &cell->value;
	}
	V &operator*()
	{
		return cell->value;
	}
	BaseIter<L> toBaseIter() const
	{
		if (cell.isNormal())
			return BaseIter<L>(cell->node, cell->index);
		auto sentinel = cell.asSpecial();
		return BaseIter<L>(sentinel->node, sentinel->node->count);
	}

	bool operator==(const PinnedIter &other) const
	{
		return cell == other.cell;
	}
	bool operator!=(const PinnedIter &other) const
	{
		return cell != other.cell;
	}

	PinnedIter &operator++()
	{
		assert(cell.isNormal() && "Cannot increment sentinel iterator");
		L *node = cell->node;
		if (cell->index + 1 < node->count)
			cell = node->get(cell->index + 1);
		else if (node->next.isSpecial())
			cell = node->next.asSpecial();
		else
			cell = node->next->get(0);
		return *this;
	}
	PinnedIter operator++(int)
	{
		PinnedIter temp = *this;
		++(*this);
		return temp;
	}
	PinnedIter &operator--()
	{
		if (cell.isSpecial())
		{
			L *node = cell.asSpecial()->node;
			cell = node->get(node->count - 1);
		}
		else
		{
			L *node = cell->node;
			assert(node->prev.isNormal() && "Cannot decrement begin iterator");
			if (cell->index > 0)
				cell = node->get(cell->index - 1);
			else
			{
				node = node->prev.asNormal();
				cell = node->get(node->count - 1);
			}
		}
		return *this;
	}
	PinnedIter operator--(int)
	{
		PinnedIter temp = *this;
		--(*this);
		return temp;
	}
};

template <typename K, typename V, uint8_t N>
struct LeafNode : public Node<K, N>
{
	using Cell = PinnedCell<V, LeafNode>;
	using NodePtr = TaggedPtr<LeafNode, SentinelNode<LeafNode>>;
	std::array<Cell *, N> subs;
	NodePtr prev;
	NodePtr next;

	LeafNode() : Node<K, N>(true) {}

	Cell *get(uint8_t index)
	{
		return subs[index];
	}

	void set(uint8_t index, const K &key, Cell *value)
	{
		value->node = this;
		value->index = index;
		this->keys[index] = key;
		this->subs[index] = std::move(value);
	}

	void move(uint8_t index1, uint8_t index2)
	{
		set(index2, this->keys[index1], this->subs[index1]);
	}

	void move(uint8_t index1, LeafNode *other, uint8_t index2)
	{
		other->set(index2, this->keys[index1], this->subs[index1]);
	}
};

template <typename K, uint8_t N>
struct KeyOnlyLeafNode : public Node<K *, N>
{
	using Cell = PinnedCell<K, KeyOnlyLeafNode>;
	using NodePtr = TaggedPtr<KeyOnlyLeafNode, SentinelNode<KeyOnlyLeafNode>>;
	NodePtr prev;
	NodePtr next;

	KeyOnlyLeafNode() : Node<K *, N>(true) {}

	Cell *get(uint8_t index)
	{
		return Cell::cellOf(this->keys[index]);
	}

	void set(uint8_t index, Cell *key)
	{
		key->node = this;
		key->index = index;
		this->keys[index] = &key->value;
	}

	void move(uint8_t index1, uint8_t index2)
	{
		set(index2, get(index1));
	}

	void move(uint8_t index1, KeyOnlyLeafNode *other, uint8_t index2)
	{
		other->set(index2, get(index1));
	}
};

template <typename T>
struct AddSummarizer
{
	T operator()(const T *keys, size_t count) const
	{
		T sum{};
		for (int i = 0; i < count; ++i)
			sum += keys[i];
		return sum;
	}
};

template <typename K, typename V, uint8_t N>
class Sequence : public BPlusTree<K, LeafNode<K, V, 2 * N - 1>, N, AddSummarizer<K>>
{
protected:
	using Base = BPlusTree<K, LeafNode<K, V, 2 * N - 1>, N, AddSummarizer<K>>;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

public:
	Sequence() {}
	~Sequence() {}

	class Iterator : public PinnedIter<V, LeafNode>
	{
		K offset{0};
		friend class Sequence;

	public:
		using Base = PinnedIter<V, LeafNode>;

		Iterator(LeafNode *node = nullptr, uint8_t index = 0, K offset = 0)
			: Base(node, index), offset(offset) {}
		Iterator(const typename LeafNode::Cell *cell, K offset)
			: Base(cell), offset(offset) {}
		Iterator(const typename LeafNode::Cell *cell)
			: Base(cell)
		{
			update();
		}
		Iterator(V *val)
			: Base(val)
		{
			update();
		}
		Iterator(SentinelNode<LeafNode> *cell, K offset = 0)
			: Base(cell), offset(offset) {}

		void update()
		{
			uint8_t index = this->cell->index;
			for (Node *current = this->cell->node; current; current = current->parent)
			{
				for (int i = 0; i < index; ++i)
					offset += current->keys[i];
				index = current->index;
			}
		}

		// key and value can be modified, but remember to call update
		K &key()
		{
			return this->leaf()->keys[this->cell->index];
		}
		K position() const
		{
			return offset;
		}
		Iterator &operator++()
		{
			offset += this->leaf()->keys[this->cell->index];
			Base::operator++();
			return *this;
		}
		Iterator operator++(int)
		{
			Iterator temp = *this;
			++(*this);
			return temp;
		}
		Iterator &operator--()
		{
			Base::operator--();
			offset -= this->leaf()->keys[this->cell->index];
			return *this;
		}
		Iterator operator--(int)
		{
			Iterator temp = *this;
			--(*this);
			return temp;
		}
	};

	Iterator begin() const
	{
		return Iterator(this->first->subs[0]);
	}

	Iterator end() const
	{
		return Iterator(this->last->next.asSpecial(), AddSummarizer<K>()(this->root->keys.data(), this->root->count));
	}

	template <typename T, typename Compare = std::less<>>
	Iterator find(const T &pos, const Compare &cmp = Compare()) const
	{
		Node *current = this->root;
		K accumulated{};
		uint8_t index = 0;
		while (1)
		{
			for (index = 0; index < current->count; ++index)
			{
				if (cmp(pos, accumulated + current->keys[index]))
					break;
				accumulated += current->keys[index];
			}
			if (index >= current->count)
				return end();
			if (current->is_leaf)
				break;
			current = static_cast<InternalNode *>(current)->subs[index];
		}
		return Iterator(static_cast<LeafNode *>(current), index, accumulated);
	}

	Iterator insertBefore(Iterator it, V value)
	{
		auto key = value.size();
		auto offset = it.position();
		auto cell = new LeafNode::Cell(std::move(value));
		auto base_it = it.toBaseIter();
		base_it = this->insertLeaf(base_it.node, base_it.index, key, cell);
		return Iterator(base_it.node, base_it.index, offset);
	}

	Iterator insertAfter(Iterator it, V value)
	{
		return insertBefore(++it, std::move(value));
	}

	void update(Iterator begin, Iterator end)
	{
		std::vector<Node *> stack;
		for (Node *current = begin.leaf(); current; current = current->parent)
		{
			stack.push_back(current);
		}

		for (;;)
		{
			LeafNode *current = static_cast<LeafNode *>(stack[0]);
			for (uint8_t i = 0; i < current->count; ++i)
				current->keys[i] = current->subs[i]->value.size();
			int l = 1;
			for (; l < stack.size(); ++l)
			{
				uint8_t index = stack[l - 1]->index;
				stack[l]->keys[index] = AddSummarizer<K>()(stack[l - 1]->keys.data(), stack[l - 1]->count);
				if (index + 1 < stack[l]->count)
				{
					stack[l - 1] = static_cast<InternalNode *>(stack[l])->subs[index + 1];
					break;
				}
			}
			if (current == end.leaf())
			{
				for (++l; l < stack.size(); ++l)
				{
					uint8_t index = stack[l - 1]->index;
					stack[l]->keys[index] = AddSummarizer<K>()(stack[l - 1]->keys.data(), stack[l - 1]->count);
				}
				break;
			}
			for (l = l - 1; l > 0; --l)
				stack[l - 1] = static_cast<InternalNode *>(stack[l])->subs[0];
		}
	}

	void update(Iterator it)
	{
		for (Node *current = it.node; current->parent; current = current->parent)
		{
			K new_key = AddSummarizer<K>()(current->keys.data(), current->count);
			if (new_key != current->parent->keys[current->index])
				current->parent->keys[current->index] = new_key;
			else
				break;
		}
	}
};

template <typename T>
struct MaxSummarizer
{
	T operator()(const T *keys, size_t count) const
	{
		return keys[count - 1];
	}
};

template <typename V, uint8_t N>
class OrderedSet : public BPlusTree<V *, KeyOnlyLeafNode<V, 2 * N - 1>, N, MaxSummarizer<V *>>
{
protected:
	using Base = BPlusTree<V *, KeyOnlyLeafNode<V, 2 * N - 1>, N, MaxSummarizer<V *>>;
	using Node = typename Base::Node;
	using InternalNode = typename Base::InternalNode;
	using LeafNode = typename Base::LeafNode;

public:
	OrderedSet() {}
	~OrderedSet() {}

	using Iterator = PinnedIter<V, LeafNode>;

	Iterator begin() const
	{
		return Iterator(this->first, 0);
	}

	Iterator end() const
	{
		return Iterator(this->last->next.asSpecial());
	}

	template <typename T, typename Compare = std::less<>>
	Iterator find(const T &key, const Compare &cmp = Compare()) const
	{
		Node *current = this->root;
		size_t index = 0;
		while (1)
		{
			auto it = std::lower_bound(
				current->keys.data(), current->keys.data() + current->count, key,
				[cmp](const V *a, const T &b)
			{
				return cmp(*a, b);
			});
			index = it - current->keys.data();
			if (index >= current->count)
				return end();
			if (current->is_leaf)
				break;
			current = static_cast<InternalNode *>(current)->subs[index];
		}
		return Iterator(static_cast<LeafNode *>(current), static_cast<uint8_t>(index));
	}

	template <typename Compare = std::less<V>>
	Iterator insert(V value, const Compare &cmp = Compare())
	{
		auto it = find(value, cmp);
		auto *cell = new LeafNode::Cell(std::move(value));
		auto base_it = it.toBaseIter();
		base_it = this->insertLeaf(base_it.node, base_it.index, cell);
		return Iterator(base_it.node, base_it.index);
	}
};
