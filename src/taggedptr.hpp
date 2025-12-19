#pragma once

#include <cstdint>

template <typename NormalType, typename SpecialType>
// requires(alignof(SpecialType) >= 2) && (alignof(NormalType) >= 2)
class TaggedPtr
{
private:
	void *m_ptr = nullptr;

	static constexpr uintptr_t Tag_Mask = 1;
	static constexpr uintptr_t Ptr_Mask = ~Tag_Mask;

public:
	TaggedPtr() : m_ptr(nullptr) {}
	TaggedPtr(const SpecialType *ptr) { set(ptr); }
	TaggedPtr(const NormalType *ptr) { set(ptr); }
	TaggedPtr &operator=(const TaggedPtr &other)
	{
		m_ptr = other.m_ptr;
		return *this;
	}
	NormalType *operator=(NormalType *ptr)
	{
		set(ptr);
		return ptr;
	}
	SpecialType *operator=(SpecialType *ptr)
	{
		set(ptr);
		return ptr;
	}

	NormalType *operator->() const
	{
		return asNormal();
	}

	operator NormalType *() const
	{
		return asNormal();
	}

	bool operator==(const TaggedPtr &other) const
	{
		return m_ptr == other.m_ptr;
	}

	bool operator!=(const TaggedPtr &other) const
	{
		return m_ptr != other.m_ptr;
	}

	bool isNormal() const { return (reinterpret_cast<uintptr_t>(m_ptr) & Tag_Mask) == 0; }
	bool isSpecial() const { return (reinterpret_cast<uintptr_t>(m_ptr) & Tag_Mask) != 0; }

	void set(const SpecialType *ptr)
	{
		uintptr_t raw = reinterpret_cast<uintptr_t>(ptr);
		assert((raw & Tag_Mask) == 0 && "Pointer must be aligned");
		m_ptr = reinterpret_cast<void *>(raw | Tag_Mask);
	}

	void set(const NormalType *ptr)
	{
		uintptr_t raw = reinterpret_cast<uintptr_t>(ptr);
		assert((raw & Tag_Mask) == 0 && "Pointer must be aligned");
		m_ptr = reinterpret_cast<void *>(raw);
	}

	NormalType *asNormal() const
	{
		assert(isNormal() && "Pointer is not normal type");
		return reinterpret_cast<NormalType *>(m_ptr);
	}

	SpecialType *asSpecial() const
	{
		assert(isSpecial() && "Pointer is not special type");
		uintptr_t raw = reinterpret_cast<uintptr_t>(m_ptr);
		return reinterpret_cast<SpecialType *>(raw & Ptr_Mask);
	}

	void *raw() const { return m_ptr; }
};

template <typename T>
class StatedPtr
{
private:
	T *m_ptr;

	static constexpr uintptr_t Error_State = 1;

public:
	StatedPtr() : m_ptr(reinterpret_cast<T *>(1)) {}
	StatedPtr(T *ptr)
		: m_ptr(ptr) {}

	StatedPtr &operator=(const StatedPtr &other)
	{
		m_ptr = other.m_ptr;
		return *this;
	}
	T *operator=(T *ptr)
	{
		m_ptr = ptr;
		return ptr;
	}

	T *operator->() const
	{
		assert(isGood() && "Pointer is in bad state");
		return m_ptr;
	}

	operator T *() const
	{
		assert(isGood() && "Pointer is in bad state");
		return m_ptr;
	}

	bool operator==(T *other) const
	{
		return m_ptr == other;
	}

	bool operator!=(T *other) const
	{
		return m_ptr != other;
	}

	void setBad()
	{
		m_ptr = reinterpret_cast<T *>(Error_State);
	}

	bool isBad() const
	{
		return reinterpret_cast<uintptr_t>(m_ptr) == Error_State;
	}
	bool isGood() const
	{
		return !isBad();
	}
};