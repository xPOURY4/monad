#pragma once

#include <monad/config.hpp>

#include <cstddef>

#include <mimalloc.h>

MONAD_NAMESPACE_BEGIN

/**
 * mimalloc allocator for use in boost object pool
 */
class PoolAllocator
{
    mi_heap_t *const heap_;

public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;

    PoolAllocator();

    [[gnu::always_inline]] mi_heap_t *get_heap() const
    {
        return heap_;
    }

    [[gnu::always_inline]] char *malloc(size_type const size)
    {
        return static_cast<char *>(mi_heap_malloc(heap_, size));
    }

    [[gnu::always_inline]] static void free(char *const p)
    {
        mi_free(p);
    }
};

MONAD_NAMESPACE_END
