#pragma once

#include <monad/trie/config.hpp>

#include <boost/pool/object_pool.hpp>

#include <concepts>
#include <memory>
#include <stdexcept>

MONAD_TRIE_NAMESPACE_BEGIN

template <class T>
concept allocator = requires(T a, size_t n) { a.deallocate(a.allocate(n), n); };

template <allocator Alloc, Alloc &(*GetAllocator)()>
struct unique_ptr_allocator_deleter
{
    using allocator_type = Alloc;
    using value_type = typename Alloc::value_type;

    constexpr unique_ptr_allocator_deleter() = default;

    constexpr void operator()(value_type *p) const
    {
        using allocator_traits = std::allocator_traits<allocator_type>;
        Alloc &alloc = GetAllocator();
        allocator_traits::destroy(alloc, p);
        allocator_traits::deallocate(alloc, p, 1);
    }
};

template <allocator Alloc, Alloc &(*GetAllocator)(), class... Args>
    requires(std::is_constructible_v<typename Alloc::value_type, Args...>)
constexpr inline std::unique_ptr<
    typename Alloc::value_type,
    unique_ptr_allocator_deleter<Alloc, GetAllocator>>
allocate_unique(Args &&...args)
{
    using allocator_traits = std::allocator_traits<Alloc>;
    Alloc &alloc = GetAllocator();
    auto *p = allocator_traits::allocate(alloc, 1);
    try {
        allocator_traits::construct(alloc, p, static_cast<Args &&>(args)...);
        return std::unique_ptr<
            typename Alloc::value_type,
            unique_ptr_allocator_deleter<Alloc, GetAllocator>>(p);
    }
    catch (...) {
        allocator_traits::deallocate(alloc, p, 1);
        throw;
    }
}

template <class T>
class boost_object_pool_allocator
{
    using _impl_type = boost::object_pool<T>;
    _impl_type _impl;

public:
    using value_type = T;
    using size_type = typename _impl_type::size_type;
    using difference_type = typename _impl_type::difference_type;

    boost_object_pool_allocator() = default;
    [[nodiscard]] T *allocate(size_t n)
    {
        if (n != 1) {
            throw std::invalid_argument("only support n = 1");
        }
        auto *ret = _impl.malloc();
        if (ret == nullptr) {
            throw std::bad_alloc();
        }
        return ret;
    }
    void deallocate(T *p, size_t n) noexcept
    {
        if (n != 1) {
            std::terminate();
        }
        _impl.free(p);
    }
};

MONAD_TRIE_NAMESPACE_END