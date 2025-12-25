#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "crdt.hpp"

class StoredRangeOp;
class Formats;

namespace
{

// a COW (copy-on-write) container for text format
class FormatArray
{
private:
	size_t ref_count;
	const size_t count;
	unsigned char raw[0]; // flexible array member

	FormatArray(size_t count)
		: ref_count(1), count(count)
	{
	}

	StyleName *styleNames()
	{
		return reinterpret_cast<StyleName *>(raw);
	}

	const StyleName *styleNames() const
	{
		return reinterpret_cast<const StyleName *>(raw);
	}

	StoredRangeOp **formatOps()
	{
		constexpr std::size_t align = alignof(StoredRangeOp *);
		std::size_t names_bytes = count * sizeof(StyleName);
		std::size_t padding = (align - (names_bytes % align)) % align;
		return reinterpret_cast<StoredRangeOp **>(raw + names_bytes + padding);
	}

	StoredRangeOp *const *formatOps() const
	{
		constexpr std::size_t align = alignof(StoredRangeOp *);
		std::size_t names_bytes = count * sizeof(StyleName);
		std::size_t padding = (align - (names_bytes % align)) % align;
		return reinterpret_cast<StoredRangeOp *const *>(raw + names_bytes + padding);
	}

	friend class ::Formats;
	friend FormatArray *createFormatArray(std::size_t);
	friend void retainFormatArray(FormatArray *);
	friend void releaseFormatArray(FormatArray *&);
};

inline FormatArray *createFormatArray(std::size_t count)
{
	// mem structure:
	// 1. FormatArray header
	// 2. `count` style names
	// 3. padding to align pointer array
	// 4. `count` pointers to format operations
	constexpr std::size_t align = alignof(StoredRangeOp *);
	std::size_t names_bytes = count * sizeof(StyleName);
	std::size_t padding = (align - (names_bytes % align)) % align;
	std::size_t ops_bytes = count * sizeof(StoredRangeOp *);
	std::size_t total_bytes = sizeof(FormatArray) + names_bytes + padding + ops_bytes;
	unsigned char *raw = new unsigned char[total_bytes];
	FormatArray *fa = new (raw) FormatArray(count);
	return fa;
}

inline void retainFormatArray(FormatArray *fa)
{
	if (fa)
		++fa->ref_count;
}

inline void releaseFormatArray(FormatArray *&fa)
{
	if (!fa)
		return;
	if (--fa->ref_count == 0)
	{
		char *raw = reinterpret_cast<char *>(fa);
		fa->~FormatArray();
		delete[] raw;
	}
	fa = nullptr;
}

} // namespace

class Formats
{
private:
	FormatArray *formats{nullptr};

	static std::vector<std::pair<StyleName, StoredRangeOp *>>
	toVector(const FormatArray *fa)
	{
		std::vector<std::pair<StyleName, StoredRangeOp *>> result;
		if (!fa)
			return result;
		result.reserve(fa->count);
		for (std::size_t i = 0; i < fa->count; ++i)
		{
			result.emplace_back(fa->styleNames()[i], fa->formatOps()[i]);
		}
		return result;
	}

	void assign(std::vector<std::pair<StyleName, StoredRangeOp *>> style_ops)
	{
		if (style_ops.empty())
		{
			releaseFormatArray(formats);
			return;
		}

		std::sort(style_ops.begin(), style_ops.end(),
				  [](const auto &a, const auto &b)
		{
			return a.first < b.first;
		});

		FormatArray *fa = createFormatArray(style_ops.size());
		for (std::size_t i = 0; i < style_ops.size(); ++i)
		{
			fa->styleNames()[i] = style_ops[i].first;
			fa->formatOps()[i] = style_ops[i].second;
		}

		releaseFormatArray(formats);
		formats = fa;
	}

public:
	Formats() = default;

	explicit Formats(std::vector<std::pair<StyleName, StoredRangeOp *>> style_ops)
	{
		assign(std::move(style_ops));
	}

	Formats(const Formats &other)
		: formats(other.formats)
	{
		retainFormatArray(formats);
	}

	Formats(Formats &&other) noexcept
		: formats(other.formats)
	{
		other.formats = nullptr;
	}

	Formats &operator=(const Formats &other)
	{
		if (this == &other)
			return *this;
		retainFormatArray(other.formats);
		releaseFormatArray(formats);
		formats = other.formats;
		return *this;
	}

	Formats &operator=(Formats &&other) noexcept
	{
		if (this == &other)
			return *this;
		releaseFormatArray(formats);
		formats = other.formats;
		other.formats = nullptr;
		return *this;
	}

	~Formats()
	{
		releaseFormatArray(formats);
	}

	bool empty() const
	{
		return !formats || formats->count == 0;
	}

	std::size_t size() const
	{
		return formats ? formats->count : 0;
	}

	StoredRangeOp *get(StyleName name) const
	{
		if (!formats)
			return nullptr;
		for (std::size_t i = 0; i < formats->count; ++i)
		{
			if (formats->styleNames()[i] == name)
				return formats->formatOps()[i];
		}
		return nullptr;
	}

	bool has(StyleName name) const
	{
		return get(name) != nullptr;
	}

	StoredRangeOp *operator[](StyleName name) const
	{
		return get(name);
	}

	void clear()
	{
		releaseFormatArray(formats);
	}

	void set(StyleName name, StoredRangeOp *op)
	{
		auto style_ops = toVector(formats);
		auto it = std::find_if(style_ops.begin(), style_ops.end(),
							   [name](const auto &p)
		{
			return p.first == name;
		});

		if (op)
		{
			if (it != style_ops.end())
				it->second = op;
			else
				style_ops.emplace_back(name, op);
		}
		else
		{
			if (it != style_ops.end())
				style_ops.erase(it);
		}

		assign(std::move(style_ops));
	}

	void remove(StyleName name)
	{
		set(name, nullptr);
	}

	void add(std::vector<std::pair<StyleName, StoredRangeOp *>> style_ops)
	{
		auto current = toVector(formats);
		current.insert(current.end(),
					   std::make_move_iterator(style_ops.begin()),
					   std::make_move_iterator(style_ops.end()));
		assign(std::move(current));
	}

	std::vector<std::pair<StyleName, StoredRangeOp *>> toVector() const
	{
		return toVector(formats);
	}
};