#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/likely.h>

#include <monad/core/assert.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

MONAD_TRIE_NAMESPACE_BEGIN

template <class T>
class offset_ptr_t
{
    int32_t offset_;

    using C = std::conditional_t<std::is_const_v<T>, char const, char>;

public:
    constexpr offset_ptr_t()
        : offset_{}
    {
    }

    constexpr offset_ptr_t(std::nullptr_t)
        : offset_{}
    {
    }

    offset_ptr_t(T *const ptr)
        : offset_{}
    {
        if (MONAD_TRIE_LIKELY(ptr)) {
            auto const offset =
                reinterpret_cast<C *>(ptr) - reinterpret_cast<C *>(this);
            // MONAD_ASSERT(offset >= INT32_MIN && offset <= INT32_MAX);
            offset_ = static_cast<int32_t>(offset);
        }
    }

    offset_ptr_t(offset_ptr_t const &other)
        : offset_ptr_t(other.get())
    {
    }

    T *get() const
    {
        auto *const this_mut = const_cast<offset_ptr_t *>(this);
        return offset_ ? reinterpret_cast<T *>(
                             reinterpret_cast<C *>(this_mut) + offset_)
                       : nullptr;
    }

    constexpr explicit operator bool() const { return offset_; }

    T &operator*() const { return *get(); }

    T *operator->() const { return get(); }
};

template <class T>
bool operator==(offset_ptr_t<T> const &x, offset_ptr_t<T> const &y)
{
    return x.get() == y.get();
}

MONAD_TRIE_NAMESPACE_END
