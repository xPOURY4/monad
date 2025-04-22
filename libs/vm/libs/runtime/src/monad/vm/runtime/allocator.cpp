#include <monad/vm/runtime/allocator.hpp>

namespace monad::vm::runtime
{
    thread_local CacheList EvmStackAllocator::stack_pool;
}
