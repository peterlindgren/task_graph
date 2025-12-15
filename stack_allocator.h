#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <cassert>

namespace detail
{
	void *fallback_alloc(std::size_t n, std::size_t alignment);
	void fallback_free(void *p);
} // detail

/**
 * Bump allocator with stack buffer and aligned heap fallback.
 *
 * Individual deallocations are no-ops for arena memory.
 * Call reset() to reclaim all arena memory at once.
 * Not thread-safe.
 */

template <std::size_t N, std::size_t Alignment = alignof(std::max_align_t)>
class StackArena
{
	static_assert((Alignment & (Alignment - 1)) == 0, "Alignment must be a power of two");
	static_assert(Alignment > 0, "Alignment must be non-zero");

public:
	StackArena() noexcept : _ptr(_buffer) {}
	StackArena(const StackArena &) = delete;
	StackArena &operator=(const StackArena &) = delete;

	void *allocate(std::size_t n, std::size_t alignment)
	{
		assert((alignment & (alignment - 1)) == 0 && "alignment must be power of 2");

		char *aligned = align_ptr(_ptr, alignment);

		// Check for pointer arithmetic overflow
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(aligned);
		if (n > std::uintptr_t(-1) - addr)
			return detail::fallback_alloc(n, alignment);

		char *end = aligned + n;

		if (end <= _buffer + N)
		{
			_ptr = end;
			return aligned;
		}

		return detail::fallback_alloc(n, alignment);
	}

	/** No-op for arena memory. Fallback allocations freed immediately. */
	void deallocate(void *p, std::size_t n) noexcept
	{
		if (!owns(p))
			detail::fallback_free(p);
	}

	void reset() noexcept { _ptr = _buffer; }

	std::size_t used() const noexcept { return static_cast<std::size_t>(_ptr - _buffer); }
	std::size_t capacity() const noexcept { return N; }

private:

	inline bool owns(void *p) const noexcept
	{
		std::less<const char *> less;
		const char *pc = static_cast<const char *>(p);
		return !less(pc, _buffer) && less(pc, _buffer + N);
	}

	static inline char *align_ptr(char *p, std::size_t alignment) noexcept
	{
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(p);
		std::uintptr_t misalign = addr & (alignment - 1);
		std::uintptr_t adjust = (alignment - misalign) & (alignment - 1);
		return reinterpret_cast<char *>(addr + adjust);
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

	using propagate_on_container_copy_assignment = std::false_type;
	using propagate_on_container_move_assignment = std::false_type;
	using propagate_on_container_swap = std::false_type;
	using is_always_equal = std::false_type;

	template <typename U>
	struct rebind { using other = StackAllocator<U, N, Alignment>; };

	StackAllocator(arena_type &arena) noexcept
		: _arena(&arena) {}

	template <typename U>
	StackAllocator(const StackAllocator<U, N, Alignment> &o) noexcept
		: _arena(o._arena) {}

	T *allocate(std::size_t n)
	{
		if (n > std::size_t(-1) / sizeof(T))
			throw std::bad_alloc();

		return static_cast<T *>(_arena->allocate(n * sizeof(T), alignof(T)));
	}

	void deallocate(T *p, std::size_t n) noexcept
	{
		_arena->deallocate(p, n * sizeof(T)); //, alignof(T));
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
