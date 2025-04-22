#pragma once

#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <cstdlib>
#include <functional>
#include <memory>
#include <vector>

namespace monad::vm::runtime
{

    struct CacheElement
    {
        CacheElement *next;
        size_t idx;
    };

    class CacheList
    {
        CacheElement *elements;

    public:
        CacheList()
            : elements{nullptr}
        {
        }

        [[gnu::always_inline]]
        bool empty()
        {
            return elements == nullptr;
        }

        [[gnu::always_inline]]
        size_t size()
        {
            return empty() ? 0 : elements->idx;
        }

        void push(CacheElement *e)
        {
            e->next = elements;
            e->idx = size() + 1;
            elements = e;
        }

        CacheElement *pop()
        {
            MONAD_VM_DEBUG_ASSERT(!empty());
            auto *ptr = elements;
            elements = ptr->next;
            return ptr;
        }

        ~CacheList()
        {
            auto e = elements;
            while (e) {
                auto next = e->next;
                std::free(e);
                e = next;
            }
        }
    };

    class EvmStackAllocator
    {
    public:
        static constexpr size_t alignment = 32;
        static constexpr size_t stack_size =
            sizeof(::monad::vm::utils::uint256_t) * 1024;
        static constexpr size_t DEFAULT_MAX_STACK_CACHE_BYTE_SIZE =
            64 * stack_size;

        static_assert(sizeof(::monad::vm::utils::uint256_t) % alignment == 0);
        static_assert(stack_size % alignment == 0);
        static_assert(sizeof(CacheElement) <= stack_size);

        /// Create an EVM stack allocator which will allow up to
        /// `max_cache_byte_size_per_thread` number of bytes being
        /// consumed by each (thread local) cache.
        EvmStackAllocator(size_t max_cache_byte_size_per_thread)
        {
            max_stacks_in_cache = max_cache_byte_size_per_thread / stack_size;
        };

        /// Get a 32 byte aligned EVM stack.
        uint8_t *aligned_alloc_evm_stack()
        {
            if (stack_pool.empty()) {
                return reinterpret_cast<uint8_t *>(
                    std::aligned_alloc(alignment, stack_size));
            }
            else {
                return reinterpret_cast<uint8_t *>(stack_pool.pop());
            }
        }

        /// Free a stack, allocated with `aligned_alloc_evm_stack`.
        void free_evm_stack(uint8_t *ptr)
        {
            if (stack_pool.size() >= max_stacks_in_cache) {
                std::free(ptr);
            }
            else {
                stack_pool.push(reinterpret_cast<CacheElement *>(ptr));
            }
        };

        std::unique_ptr<uint8_t, std::function<void(uint8_t *)>>
        allocate_stack()
        {
            return {aligned_alloc_evm_stack(), [&](uint8_t *ptr) {
                        free_evm_stack(ptr);
                    }};
        }

    private:
        /// Upper bound on the number of stacks in each cache.
        size_t max_stacks_in_cache;
        static thread_local CacheList stack_pool;
    };
}
