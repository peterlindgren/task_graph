#include "stack_allocator.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


namespace detail
{

void *fallback_alloc(std::size_t n, std::size_t alignment)
{
	return _aligned_malloc(n, alignment);
}

void fallback_free(void *p)
{
	_aligned_free(p);
}

} // detail
