#include <monad/mem/pool_allocator.hpp>

#include <mimalloc.h>

MONAD_NAMESPACE_BEGIN

PoolAllocator::PoolAllocator()
    : heap_{mi_heap_get_default()}
{
}

char *PoolAllocator::malloc(size_type const size)
{
    return static_cast<char *>(mi_heap_malloc(heap_, size));
}

void PoolAllocator::free(char *const p)
{
    mi_free(p);
}

MONAD_NAMESPACE_END
