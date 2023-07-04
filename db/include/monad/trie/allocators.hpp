#pragma once

#include <monad/trie/config.hpp>

#include <monad/core/assert.h>
#include <monad/core/cmemory.hpp>

#include <boost/pool/pool.hpp>

#include <concepts>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

#ifndef MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    #if defined(__SANITIZE_ADDRESS__)
        #define MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR 1
    #elif defined(__SANITIZE_THREAD__)
        #define MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR 1
    #elif defined(__SANITIZE_UNDEFINED__)
        #define MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR 1
    #endif
#endif

MONAD_TRIE_NAMESPACE_BEGIN

namespace detail
{
    template <class T>
    struct is_unique_ptr : public std::false_type
    {
    };
    template <class T, class Deleter>
    struct is_unique_ptr<std::unique_ptr<T, Deleter>> : public std::true_type
    {
    };
}

//! \brief Concept matching a STL allocator.
template <class T>
concept allocator = requires(T a, size_t n) { a.deallocate(a.allocate(n), n); };

//! \brief Concept matching a Deleter template parameter for a `unique_ptr`.
template <class T, class U>
concept unique_ptr_deleter = requires(T x, U *p) { x(p); };

//! \brief Concept matching a `unique_ptr`.
template <class T>
concept unique_ptr = detail::is_unique_ptr<std::decay_t<T>>::value;

//! \brief If a type opts into this, `calloc_free_allocator` and `calloc` is
//! used and constructors are NOT called. Only opt into this if your type is
//! happy seeing all bytes zero as if it had been constructed, this can be a win
//! for same use cases (`calloc` may be able to avoid zeroing memory if it knows
//! its source is already zeroed) but can also be a loss if zeroing bytes is
//! more expensive than calling the constructor.
//!
//! \warning Be SURE to specialise this before instantiating any code which
//! instantiates allocator code! Otherwise it will have no effect.
template <class T>
struct construction_equals_all_bits_zero : std::false_type
{
};

//! \brief Injects a noop `construct()` into a STL allocator.
template <allocator Base>
struct disable_construct_in_allocator : public Base
{
    template <class U, class... Args>
    void construct(U *, Args &&...)
    {
    }
};

/**************************************************************************/
//! \brief A STL allocator which uses `malloc`-`free`.
template <class T>
struct malloc_free_allocator
{
    using value_type = T;
    [[nodiscard]] constexpr T *allocate(std::size_t no)
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        if constexpr (alignof(T) > alignof(max_align_t)) {
            return reinterpret_cast<T *>(
                std::aligned_alloc(alignof(T), no * sizeof(T)));
        }
        return reinterpret_cast<T *>(std::malloc(no * sizeof(T)));
    }
    template <class U>
    [[nodiscard]] constexpr T *allocate_overaligned(std::size_t no)
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        if constexpr (alignof(U) > alignof(max_align_t)) {
            return reinterpret_cast<T *>(
                std::aligned_alloc(alignof(U), no * sizeof(T)));
        }
        return reinterpret_cast<T *>(std::malloc(no * sizeof(T)));
    }
    constexpr void deallocate(T *p, std::size_t)
    {
        std::free(p);
    }
};

//! \brief A STL allocator which uses `calloc`-`free` (i.e. allocated bytes are
//! returned zeroed)
template <class T>
struct calloc_free_allocator
{
    using value_type = T;
    [[nodiscard]] constexpr T *allocate(std::size_t no)
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        if constexpr (alignof(T) > alignof(max_align_t)) {
            char *ret = reinterpret_cast<char *>(
                std::aligned_alloc(alignof(T), no * sizeof(T)));
            cmemset(ret, char(0), no * sizeof(T));
            return reinterpret_cast<T *>(ret);
        }
        return reinterpret_cast<T *>(std::calloc(no, sizeof(T)));
    }
    template <class U>
    [[nodiscard]] constexpr T *allocate_overaligned(std::size_t no)
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        if constexpr (alignof(U) > alignof(max_align_t)) {
            char *ret = reinterpret_cast<char *>(
                std::aligned_alloc(alignof(U), no * sizeof(T)));
            cmemset(ret, char(0), no * sizeof(T));
            return reinterpret_cast<T *>(ret);
        }
        return reinterpret_cast<T *>(std::calloc(no, sizeof(T)));
    }
    constexpr void deallocate(T *p, std::size_t)
    {
        std::free(p);
    }
};

//! \brief Chooses `calloc_free_allocator` if
//! `construction_equals_all_bits_zero<T>` is true, else `malloc_free_allocator`
template <class T, class U = T>
using calloc_free_allocator_if_opted_in = std::conditional_t<
    construction_equals_all_bits_zero<T>::value, calloc_free_allocator<U>,
    malloc_free_allocator<U>>;

/**************************************************************************/
//! \brief A unique ptr deleter for a STL allocator
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

namespace detail
{
    template <allocator TypeAlloc, allocator RawAlloc>
    struct type_raw_alloc_pair
    {
        using type_allocator = TypeAlloc;
        using raw_bytes_allocator = RawAlloc;

        TypeAlloc &type_alloc;
        RawAlloc &raw_alloc;
    };
    template <class T>
    inline type_raw_alloc_pair<
        std::allocator<T>, calloc_free_allocator_if_opted_in<T, std::byte>>
    GetStdAllocatorPair()
    {
        static std::allocator<T> a;
        static calloc_free_allocator_if_opted_in<T, std::byte> b;
        return {a, b};
    }
}

//! \brief A unique ptr deleter for a STL allocator where underlying storage
//! exceeds type
template <
    allocator TypeAlloc, allocator RawAlloc,
    detail::type_raw_alloc_pair<TypeAlloc, RawAlloc> (*GetAllocator)(),
    size_t (*GetSize)(typename TypeAlloc::value_type *)>
struct unique_ptr_aliasing_allocator_deleter
{
    using allocator_type = TypeAlloc;
    using value_type = typename TypeAlloc::value_type;
    constexpr unique_ptr_aliasing_allocator_deleter() {}
    constexpr void operator()(value_type *p1) const
    {
        using allocator1_traits = std::allocator_traits<allocator_type>;
        using allocator2_traits = std::allocator_traits<RawAlloc>;
        // Use all bits one for the number of items to deallocate in
        // order to trap use of unsuitable user supplied allocators
        typename allocator2_traits::size_type no(-1);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored                                                 \
    "-Waddress" // warns about GetSize never being null
        if (GetSize != nullptr) {
            no = GetSize(p1);
        }
#pragma GCC diagnostic pop
        auto [alloc1, alloc2] = GetAllocator();
        allocator1_traits::destroy(alloc1, p1);
        auto *p2 = reinterpret_cast<std::byte *>(p1);
        allocator2_traits::deallocate(alloc2, p2, no);
    }
};

//! \brief An implementation of proposed `allocate_unique`, a STL allocator
//! aware `unique_ptr`.
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
        if constexpr (!construction_equals_all_bits_zero<
                          typename Alloc::value_type>::value) {
            allocator_traits::construct(
                alloc, p, static_cast<Args &&>(args)...);
        }
        return std::unique_ptr<
            typename Alloc::value_type,
            unique_ptr_allocator_deleter<Alloc, GetAllocator>>(p);
    }
    catch (...) {
        allocator_traits::deallocate(alloc, p, 1);
        throw;
    }
}

//! \brief A STL allocator aware unique ptr whose storage is larger than its
//! type. Useful for variably lengthed types. Needs a raw storage allocator
//! capable of deallocating without being told how many bytes to deallocate.
template <
    allocator TypeAlloc, allocator RawAlloc,
    detail::type_raw_alloc_pair<TypeAlloc, RawAlloc> (*GetAllocator)(),
    size_t (*GetSize)(typename TypeAlloc::value_type *) = nullptr,
    class... Args>
    requires(
        std::is_same_v<typename RawAlloc::value_type, std::byte> &&
        std::is_constructible_v<typename TypeAlloc::value_type, Args...>)
constexpr inline std::unique_ptr<
    typename TypeAlloc::value_type,
    unique_ptr_aliasing_allocator_deleter<
        TypeAlloc, RawAlloc, GetAllocator, GetSize>>
allocate_aliasing_unique(size_t storagebytes, Args &&...args)
{
    MONAD_ASSERT(storagebytes >= sizeof(typename TypeAlloc::value_type));
    using allocator1_traits = std::allocator_traits<TypeAlloc>;
    using allocator2_traits = std::allocator_traits<RawAlloc>;
    auto [alloc1, alloc2] = GetAllocator();
    std::byte *p2;
    if constexpr (
        alignof(typename TypeAlloc::value_type) > alignof(max_align_t)) {
        p2 = alloc2
                 .template allocate_overaligned<typename TypeAlloc::value_type>(
                     storagebytes);
    }
    else {
        p2 = allocator2_traits::allocate(alloc2, storagebytes);
    }
#ifndef NDEBUG
    if constexpr (!construction_equals_all_bits_zero<
                      typename TypeAlloc::value_type>::value) {
        // Trap use of region after end of type
        cmemset(p2, std::byte{0xff}, storagebytes);
    }
#endif
    try {
        auto *p1 = reinterpret_cast<typename TypeAlloc::value_type *>(p2);
        if constexpr (!construction_equals_all_bits_zero<
                          typename TypeAlloc::value_type>::value) {
            allocator1_traits::construct(
                alloc1, p1, static_cast<Args &&>(args)...);
        }
        using deleter_type = unique_ptr_aliasing_allocator_deleter<
            TypeAlloc,
            RawAlloc,
            GetAllocator,
            GetSize>;
        return std::unique_ptr<typename TypeAlloc::value_type, deleter_type>(
            p1);
    }
    catch (...) {
        allocator2_traits::deallocate(alloc2, p2, storagebytes);
        throw;
    }
}
//! \brief A unique ptr whose storage is larger than its type, using
//! `std::allocator`.
template <class T, class... Args>
constexpr inline std::unique_ptr<
    T, unique_ptr_aliasing_allocator_deleter<
           std::allocator<T>, malloc_free_allocator<std::byte>,
           detail::GetStdAllocatorPair<T>, nullptr>>
make_aliasing_unique(size_t storagebytes, Args &&...args)
{
    return allocate_aliasing_unique<
        std::allocator<T>,
        malloc_free_allocator<std::byte>,
        detail::GetStdAllocatorPair<T>>(
        storagebytes, static_cast<Args &&>(args)...);
}

/**************************************************************************/

namespace detail
{
    // The clang 16 on CI is just plain broken here. clang 14 on my machine
    // is not broken.
#if defined(__clang__) && __clang_major__ == 16
    template <class T>
    constexpr bool is_array_v = true;
#else
    template <class T>
    constexpr bool is_array_v = std::is_array_v<T>;
#endif
}

struct unique_ptr_free_deleter
{
    template <class T>
    constexpr void operator()(T *p) const
    {
        MONAD_ASSERT(p == nullptr);
    }
};

//! \brief A unique ptr whose pointee can be resized. Requires `T[0]` to be
//! trivially copyable so storage overhead can be avoided. `owning_span` is
//! suitable for more complex types.
//!
//! \note Any custom constructor you might have gets ignored!
//! `construction_equals_all_bits_zero<T>` is respected however.
template <class T>
class resizeable_unique_ptr;
template <class T>
    requires(detail::is_array_v<T> && std::is_trivially_copyable_v<T>)
constexpr inline resizeable_unique_ptr<T>
make_resizeable_unique_for_overwrite(size_t no);
template <class T>
class resizeable_unique_ptr<T[]>
    : protected std::unique_ptr<T[], unique_ptr_free_deleter>
{
    friend constexpr resizeable_unique_ptr<T[]>
    make_resizeable_unique_for_overwrite<T[]>(size_t no);

    using _base = std::unique_ptr<T[], unique_ptr_free_deleter>;

    explicit constexpr resizeable_unique_ptr(size_t no)
        : _base([no] {
            MONAD_ASSERT(no < size_t(-1) / sizeof(T));
            calloc_free_allocator_if_opted_in<T> alloc;
            T *ret = alloc.allocate(no);
            if (ret == nullptr) {
                throw std::bad_alloc();
            }
            return ret;
        }())
    {
    }

public:
    resizeable_unique_ptr() = default;
    resizeable_unique_ptr(const resizeable_unique_ptr &) = delete;
    resizeable_unique_ptr(resizeable_unique_ptr &&) = default;
    resizeable_unique_ptr &operator=(const resizeable_unique_ptr &) = delete;
    resizeable_unique_ptr &operator=(resizeable_unique_ptr &&o) noexcept
    {
        if (this != &o) {
            this->~resizeable_unique_ptr();
            new (this)
                resizeable_unique_ptr(static_cast<resizeable_unique_ptr &&>(o));
        }
        return *this;
    }
    ~resizeable_unique_ptr()
    {
        reset();
    }
    using _base::get;
    using _base::release;
    using _base::operator bool;
    using _base::operator[];
    template <class U>
        requires(
            !std::is_same_v<U, std::nullptr_t> &&
            requires(_base x, U y) { x.reset(y); })
    void reset(U ptr) noexcept
    {
        if (_base::get() != nullptr) {
            std::free(_base::get());
            _base::release();
        }
        _base::reset(ptr);
    }
    void reset(std::nullptr_t = nullptr) noexcept
    {
        if (_base::get() != nullptr) {
            std::free(_base::get());
            _base::release();
        }
    }
    void swap(resizeable_unique_ptr &o) noexcept
    {
        _base::swap(o);
    }
    //! Try to resize the pointee, returning false if was unable
    bool try_resize(size_t no) noexcept
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        auto *ret = (T *)std::realloc(_base::get(), no * sizeof(T));
        if (ret == nullptr) {
            return false;
        }
        _base::release();
        _base::reset(ret);
        return true;
    }
    //! Resize the pointer, throwing `bad_alloc` if failed. Returns true if
    //! pointee moved in memory.
    bool resize(size_t no)
    {
        MONAD_ASSERT(no < size_t(-1) / sizeof(T));
        auto *ret = (T *)std::realloc(_base::get(), no * sizeof(T));
        if (ret == nullptr) {
            throw std::bad_alloc();
        }
        auto *old = _base::release();
        _base::reset(ret);
        return old != ret;
    }
};
template <class T>
struct detail::is_unique_ptr<resizeable_unique_ptr<T[]>> : public std::true_type
{
};
//! \brief Use this function to create a `resizeable_unique_ptr<T>`.
template <class T>
    requires(detail::is_array_v<T> && std::is_trivially_copyable_v<T>)
constexpr inline resizeable_unique_ptr<T>
make_resizeable_unique_for_overwrite(size_t no)
{
    return resizeable_unique_ptr<T>(no);
}

/**************************************************************************/

//! \brief An owning span, as we don't have `static_vector` yet.
template <class T, allocator Alloc = std::allocator<T>>
class owning_span : public std::span<T>
{
    using _base = std::span<T>;
    using _size_type = typename _base::size_type;
    [[no_unique_address]] Alloc _alloc;

    template <class... Args>
    static _base _allocate(Alloc alloc, _size_type no, Args &&...args)
    {
        using allocator_traits = std::allocator_traits<Alloc>;
        auto *p = allocator_traits::allocate(alloc, no);
        for (_size_type n = 0; n < no; n++) {
            try {
                if constexpr (!construction_equals_all_bits_zero<T>::value) {
                    allocator_traits::construct(
                        alloc, &p[n], static_cast<Args &&>(args)...);
                }
            }
            catch (...) {
                while (n > 0) {
                    allocator_traits::destroy(alloc, &p[--n]);
                }
                allocator_traits::deallocate(alloc, p, no);
                throw;
            }
        }
        return _base(p, no);
    }

public:
    using size_type = _size_type;
    owning_span() = default;
    constexpr explicit owning_span(const Alloc &alloc)
        : _alloc(alloc)
    {
    }
    constexpr explicit owning_span(
        size_type no, const T &v, const Alloc &alloc = Alloc())
        : _base(_allocate(alloc, no, v))
        , _alloc(alloc)
    {
    }
    constexpr owning_span(size_type no, const Alloc &alloc = Alloc())
        : _base(_allocate(alloc, no))
        , _alloc(alloc)
    {
    }
    owning_span(const owning_span &) = delete;
    owning_span &operator=(const owning_span &) = delete;
    owning_span(owning_span &&o) noexcept
        : _base(static_cast<_base &&>(o))
        , _alloc(static_cast<Alloc &&>(o._alloc))
    {
        auto &&other = static_cast<_base &&>(o);
        other = {};
    }
    owning_span &operator=(owning_span &&o) noexcept
    {
        if (this != &o) {
            this->~owning_span();
            new (this) owning_span(static_cast<owning_span &&>(o));
        }
        return *this;
    }
    ~owning_span()
    {
        using allocator_traits = std::allocator_traits<Alloc>;
        for (auto &i : *this) {
            allocator_traits::destroy(_alloc, &i);
        }
        if (_base::data() != nullptr) {
            allocator_traits::deallocate(_alloc, _base::data(), _base::size());
        }
    }
};

/**************************************************************************/

template <unique_ptr T>
void delayed_reset(T &&ptr);
/*! \class thread_local_delayed_unique_ptr_resetter
\brief Collects unique ptrs upon whom `delayed_reset()` is called into
a thread local list. When the resetter is reset, destroys those unique ptrs.
*/
template <unique_ptr T>
class thread_local_delayed_unique_ptr_resetter
{
    friend void delayed_reset<T>(T &&ptr);
    thread_local_delayed_unique_ptr_resetter *_prev{nullptr};
    std::vector<T> _ptrs;

    static thread_local_delayed_unique_ptr_resetter *&_inst()
    {
        static thread_local thread_local_delayed_unique_ptr_resetter *v;
        return v;
    }

    void _add(T &&v)
    {
        _ptrs.push_back(std::move(v));
    }

public:
    using unique_ptr_type = T;
    thread_local_delayed_unique_ptr_resetter()
        : _prev(_inst())
    {
        _inst() = this;
        _ptrs.reserve(256);
    }
    thread_local_delayed_unique_ptr_resetter(
        const thread_local_delayed_unique_ptr_resetter &) = delete;
    thread_local_delayed_unique_ptr_resetter(
        thread_local_delayed_unique_ptr_resetter &&) = delete;
    thread_local_delayed_unique_ptr_resetter &
    operator=(const thread_local_delayed_unique_ptr_resetter &) = delete;
    thread_local_delayed_unique_ptr_resetter &
    operator=(thread_local_delayed_unique_ptr_resetter &&) = delete;
    ~thread_local_delayed_unique_ptr_resetter()
    {
        reset();
        _inst() = _prev;
    }
    //! Returns a pointer to the instance for the calling thread, if any
    static thread_local_delayed_unique_ptr_resetter *thread_instance()
    {
        return _inst();
    }
    //! Executes destructing all the enqueued unique ptrs.
    void reset()
    {
        _ptrs.clear();
    }
};
//! \brief Delay the reset of the specified unique ptr. If there is not a
//! `thread_local_delayed_unique_ptr_resetter` instance earlier in the
//! calling thread's stack, terminates the process.
template <unique_ptr T>
void delayed_reset(T &&ptr)
{
    using resetter_type = thread_local_delayed_unique_ptr_resetter<T>;
    MONAD_ASSERT(resetter_type::thread_instance() != nullptr);
    resetter_type::thread_instance()->_add(std::move(ptr));
}

/**************************************************************************/

//! \brief A STL allocator wrapping `boost::pool<T>` using ordered allocation.
template <
    class T, typename UserAllocator = boost::default_user_allocator_new_delete>
class boost_ordered_pool_allocator
{
    using _impl_type = boost::pool<UserAllocator>;
    _impl_type _impl;

public:
    using value_type = T;
    using size_type = typename _impl_type::size_type;
    using difference_type = typename _impl_type::difference_type;

    boost_ordered_pool_allocator(
        const size_type nnext_size = 32, const size_type nmax_size = 0)
        : _impl(sizeof(T), nnext_size, nmax_size)
    {
    }
    [[nodiscard]] T *allocate(size_t n)
    {
#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        auto *ret = (T *)std::aligned_alloc(alignof(T), sizeof(T));
#else
        auto *ret = (T *)_impl.ordered_malloc(n);
#endif
        if (ret == nullptr) {
            throw std::bad_alloc();
        }
        return ret;
    }
    void deallocate(T *p, size_t n) noexcept
    {
#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        (void)n;
        std::free(p);
#else
        _impl.ordered_free(p, n);
#endif
    }
    template <class U, class... Args>
    void construct(U *p, Args &&...args)
    {
        if constexpr (construction_equals_all_bits_zero<T>::value) {
            std::memset(p, 0, sizeof(U));
        }
        else {
            std::construct_at(p, static_cast<Args &&>(args)...);
        }
    }
};

//! \brief A STL allocator wrapping `boost::pool<T>` using unordered allocation.
//! As unordered allocation does not support array allocation, that terminates
//! the process.
template <
    class T, typename UserAllocator = boost::default_user_allocator_new_delete>
class boost_unordered_pool_allocator
{
    using _impl_type = boost::pool<UserAllocator>;
    _impl_type _impl;

public:
    using value_type = T;
    using size_type = typename _impl_type::size_type;
    using difference_type = typename _impl_type::difference_type;

    boost_unordered_pool_allocator(
        const size_type nnext_size = 32, const size_type nmax_size = 0)
        : _impl(sizeof(T), nnext_size, nmax_size)
    {
    }
    [[nodiscard]] T *allocate(size_t n)
    {
        if (n != 1) {
            throw std::invalid_argument("only supports n = 1");
        }
#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        auto *ret = (T *)std::aligned_alloc(alignof(T), sizeof(T));
#else
        auto *ret = (T *)_impl.malloc();
#endif
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
#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        std::free(p);
#else
        _impl.free(p);
#endif
    }
    template <class U, class... Args>
    void construct(U *p, Args &&...args)
    {
        if constexpr (construction_equals_all_bits_zero<T>::value) {
            std::memset(p, 0, sizeof(U));
        }
        else {
            std::construct_at(p, static_cast<Args &&>(args)...);
        }
    }
};

/*! \class array_of_boost_pools_allocator
\brief A STL allocator which is an indexed array of Boost pools

If you know that you will be allocating items of a few known sizes e.g.
16, 24 and 32 bytes, and none others, it can be more efficient to use
dedicated pool for each of those sizes.
*/
template <
    size_t lower_bound, size_t upper_bound, size_t divisor,
    size_t less_than_lower_bound = 0,
    typename UserAllocator = boost::default_user_allocator_new_delete>
class array_of_boost_pools_allocator
{
    using _impl_type = boost::pool<UserAllocator>;
    static constexpr size_t _pool_count =
        ((upper_bound - lower_bound) / divisor) + (less_than_lower_bound != 0);
    // boost::pool can neither be copied nor moved, so ...
    alignas(_impl_type) std::byte
        _pool_storage[_pool_count * sizeof(_impl_type)];

    std::span<_impl_type> _pools() noexcept
    {
        return {(_impl_type *)_pool_storage, _pool_count};
    }

public:
    using value_type = std::byte;
    using size_type = typename _impl_type::size_type;
    using difference_type = typename _impl_type::difference_type;

    array_of_boost_pools_allocator(
        const size_type nnext_size = 32, const size_type nmax_size = 0)
    {
        auto _pools = this->_pools();
        for (size_t n = 0; n < _pools.size(); n++) {
            size_t itemsize = lower_bound + divisor * n;
            if (less_than_lower_bound != 0 && n == _pools.size() - 1) {
                itemsize = less_than_lower_bound;
            }
            new (&_pools[n]) _impl_type(itemsize, nnext_size, nmax_size);
        }
    }
    array_of_boost_pools_allocator(const array_of_boost_pools_allocator &) =
        delete;
    array_of_boost_pools_allocator(array_of_boost_pools_allocator &&) = delete;
    array_of_boost_pools_allocator &
    operator=(const array_of_boost_pools_allocator &) = delete;
    array_of_boost_pools_allocator &
    operator=(array_of_boost_pools_allocator &&) = delete;
    ~array_of_boost_pools_allocator()
    {
        for (auto &i : _pools()) {
            i.~_impl_type();
        }
    }

    [[nodiscard]] value_type *allocate(size_t n)
    {
        if (n >= upper_bound) {
            throw std::invalid_argument(
                "only supports n lower than upper bound");
        }
        if (less_than_lower_bound == 0 && n < lower_bound) {
            throw std::invalid_argument(
                "only supports n greater than lower bound");
        }
        if (less_than_lower_bound != 0 && n < lower_bound &&
            n > less_than_lower_bound) {
            throw std::invalid_argument(
                "if n is less than lower bound then it must be less than or "
                "equal to less_than_lower_bound");
        }
#ifndef NDEBUG
        if (n >= lower_bound) {
            const size_t idx = (n - lower_bound) / divisor;
            assert(idx * divisor + lower_bound == n);
        }
#endif

#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        auto *ret = (value_type *)std::malloc(n);
#else
        auto &_impl = (n >= lower_bound) ? _pools()[(n - lower_bound) / divisor]
                                         : _pools().back();
        auto *ret = (value_type *)_impl.malloc();
#endif
        if (ret == nullptr) {
            throw std::bad_alloc();
        }
        return ret;
    }
    void deallocate(value_type *p, size_t n) noexcept
    {
        MONAD_ASSERT(n < upper_bound);
        MONAD_ASSERT(less_than_lower_bound != 0 || n >= lower_bound);
#if MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
        std::free(p);
#else
        auto &_impl = (n >= lower_bound) ? _pools()[(n - lower_bound) / divisor]
                                         : _pools().back();
        _impl.free(p);
#endif
    }
};

/**************************************************************************/

MONAD_TRIE_NAMESPACE_END