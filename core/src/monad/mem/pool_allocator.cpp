#include <monad/mem/pool_allocator.hpp>

MONAD_NAMESPACE_BEGIN

PoolAllocator::PoolAllocator()
    : heap_{mi_heap_get_default()}
{
}

MONAD_NAMESPACE_END
