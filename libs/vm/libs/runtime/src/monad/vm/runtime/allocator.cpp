#include <monad/vm/runtime/allocator.hpp>

namespace monad::vm::runtime
{
    template <>
    thread_local CacheList
        ThreadLocalCacheList<EvmStackAllocatorMeta>::cache_pool{};
    template <>
    thread_local CacheList
        ThreadLocalCacheList<EvmMemoryAllocatorMeta>::cache_pool{};
}
