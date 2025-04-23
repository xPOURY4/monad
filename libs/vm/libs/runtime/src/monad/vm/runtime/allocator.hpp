#pragma once

#include <monad/vm/core/assert.h>
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

    template <typename T>
    struct ThreadLocalCacheList
    {
        static thread_local CacheList cache_pool;
    };

    template <typename T>
    concept Allocable = requires(T a) {
        typename T::base_type;
        { T::size } -> std::same_as<size_t const &>;
        { T::alignment } -> std::same_as<size_t const &>;
    };

    template <Allocable T>
    class CachedAllocator
    {
    public:
        static constexpr size_t alloc_size =
            sizeof(typename T::base_type) * T::size;
        static constexpr size_t DEFAULT_MAX_CACHE_BYTE_SIZE = 64 * alloc_size;

        static_assert(alloc_size % T::alignment == 0);
        static_assert(sizeof(CacheElement) <= alloc_size);

        /// Create an allocator which will allow up to
        /// `max_cache_byte_size_per_thread` number of bytes being
        /// consumed by each (thread local) cache.
        CachedAllocator(size_t max_cache_byte_size_per_thread)
        {
            max_slots_in_cache = max_cache_byte_size_per_thread / alloc_size;
        };

        /// Get a 32 byte aligned EVM stack.
        uint8_t *aligned_alloc_cached()
        {
            if (ThreadLocalCacheList<T>::cache_pool.empty()) {
                return reinterpret_cast<uint8_t *>(
                    std::aligned_alloc(T::alignment, alloc_size));
            }
            else {
                return reinterpret_cast<uint8_t *>(
                    ThreadLocalCacheList<T>::cache_pool.pop());
            }
        }

        /// Free memory allocated with `aligned_alloc_cached`.
        void free_cached(uint8_t *ptr) const
        {
            if (ThreadLocalCacheList<T>::cache_pool.size() >=
                max_slots_in_cache) {
                std::free(ptr);
            }
            else {
                ThreadLocalCacheList<T>::cache_pool.push(
                    reinterpret_cast<CacheElement *>(ptr));
            }
        };

        std::unique_ptr<uint8_t, std::function<void(uint8_t *)>> allocate()
        {
            return {aligned_alloc_cached(), [*this](uint8_t *ptr) {
                        free_cached(ptr);
                    }};
        }

    private:
        /// Upper bound on the number of stacks in each cache.
        size_t max_slots_in_cache;
    };

    struct EvmStackAllocatorMeta
    {
        using base_type = ::monad::vm::utils::uint256_t;
        static constexpr size_t size = 1024;
        static constexpr size_t alignment = 32;
    };

    template <>
    thread_local CacheList
        ThreadLocalCacheList<EvmStackAllocatorMeta>::cache_pool;

    struct EvmMemoryAllocatorMeta
    {
        using base_type = uint8_t;
        static constexpr size_t size = 4096;
        static constexpr size_t alignment = 1;
    };

    template <>
    thread_local CacheList
        ThreadLocalCacheList<EvmMemoryAllocatorMeta>::cache_pool;

    using EvmStackAllocator = CachedAllocator<EvmStackAllocatorMeta>;
    using EvmMemoryAllocator = CachedAllocator<EvmMemoryAllocatorMeta>;
}
