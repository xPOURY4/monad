#include <monad/vm/runtime/allocator.hpp>
#include <monad/vm/runtime/cached_allocator.hpp>

namespace monad::vm::runtime
{
    thread_local utils::CachedAllocatorList EvmStackAllocatorMeta::cache_list;
    thread_local utils::CachedAllocatorList EvmMemoryAllocatorMeta::cache_list;
}
