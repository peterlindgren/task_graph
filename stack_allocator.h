#pragma once

#include <cstddef>
#include <memory>

template <std::size_t N, std::size_t Alignment = alignof(std::max_align_t)>
class StackArena
{
public:
	StackArena() noexcept : _ptr(_buffer) {}
	StackArena(const StackArena &) = delete;
	StackArena &operator=(const StackArena &) = delete;

	void *allocate(std::size_t n)
	{
		char *aligned = align_ptr(_ptr, Alignment);
		char *end = aligned + n;

		if (end <= _buffer + N)
		{
			_ptr = end;
			return aligned;
		}

		return ::operator new(n);
	}

	void deallocate(void *p, std::size_t n) noexcept
	{
		if (owns(p))
		{
			if (static_cast<char *>(p) + n == _ptr)
			{
				_ptr = static_cast<char *>(p);
			}
		}
		else
		{
			::operator delete(p);
		}
	}

	void reset() noexcept { _ptr = _buffer; }

	std::size_t used() const noexcept { return static_cast<std::size_t>(_ptr - _buffer); }
	std::size_t capacity() const noexcept { return N; }

private:
	bool owns(void *p) const noexcept
	{
		char *pc = static_cast<char *>(p);
		return pc >= _buffer && pc < (_buffer + N);
	}

	static char *align_ptr(char *p, std::size_t alignment) noexcept
	{
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);
		std::uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
		return reinterpret_cast<char *>(aligned);
	}

	alignas(Alignment) char _buffer[N];
	char *_ptr;
};

template <typename T, std::size_t N = 4096, std::size_t Alignment = alignof(std::max_align_t)>
class StackAllocator
{
public:
	using value_type = T;
	using arena_type = StackArena<N, Alignment>;

	template <typename U>
	struct rebind { using other = StackAllocator<U, N, Alignment>; };

	StackAllocator(arena_type &arena) noexcept
		: _arena(&arena) {}

	template <typename U>
	StackAllocator(const StackAllocator<U, N, Alignment> &o) noexcept
		: _arena(o._arena) {}

	T *allocate(std::size_t n)
	{
		return static_cast<T *>(_arena->allocate(n * sizeof(T)));
	}

	void deallocate(T *p, std::size_t n) noexcept
	{
		_arena->deallocate(p, n * sizeof(T));
	}

	template <typename U>
	bool operator==(const StackAllocator<U, N, Alignment> &o) const noexcept
	{
		return _arena == o._arena;
	}

	template <typename U>
	bool operator!=(const StackAllocator<U, N, Alignment> &o) const noexcept
	{
		return _arena != o._arena;
	}

private:
	template <typename U, std::size_t, std::size_t>
	friend class StackAllocator;

	arena_type *_arena;
};
