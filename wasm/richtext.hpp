#pragma once

#include "piecetree.hpp"
#include <emscripten/val.h>
#include <unordered_map>

using emscripten::val;

struct StyleDescriptor
{
	int key;
	std::string name;
	RangeInterval right_open;
	val default_val;
	RangeTree<4> tree;
};

// a format provider using js object, only for wasm
class WasmFormatProvider
{
private:
	// styles stored by numeric key, name -> key in map
	std::vector<StyleDescriptor> styles;
	std::unordered_map<std::string, int> name_to_key;

public:
	WasmFormatProvider() = default;

	// register a style name, return its numeric key
	// if the style already exists, just return the existing key
	int addStyle(const std::string &name,
				 RangeInterval interval = RangeInterval::Inclusive,
				 const val &defaultValue = val::undefined())
	{
		auto it = name_to_key.find(name);
		if (it != name_to_key.end())
			return it->second;

		int key = static_cast<int>(styles.size());
		StyleDescriptor desc{};
		desc.key = key;
		desc.name = name;
		desc.right_open = interval;
		desc.default_val = defaultValue;
		styles.push_back(std::move(desc));
		name_to_key.emplace(styles.back().name, key);
		return key;
	}

	// get style descriptor by name, nullptr if not found
	StyleDescriptor *getStyle(const std::string &name)
	{
		auto it = name_to_key.find(name);
		if (it == name_to_key.end())
			return nullptr;
		return &styles[static_cast<std::size_t>(it->second)];
	}

	const StyleDescriptor *getStyle(const std::string &name) const
	{
		auto it = name_to_key.find(name);
		if (it == name_to_key.end())
			return nullptr;
		return &styles[static_cast<std::size_t>(it->second)];
	}

	// PieceCRDT formatting integration: map Formatting.op.key (string) -> style key
	template <typename RangeType, typename T>
	int styleKey(const Formatting<RangeType, T> &op)
	{
		auto it = name_to_key.find(op.key);
		if (it == name_to_key.end())
			return -1; // unknown style name
		return it->second;
	}

	// access the RangeTree for a given style key
	RangeTree<4> &style(int key)
	{
		return styles[static_cast<std::size_t>(key)].tree;
	}

	const RangeTree<4> &style(int key) const
	{
		return styles[static_cast<std::size_t>(key)].tree;
	}
};
