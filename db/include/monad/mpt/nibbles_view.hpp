#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

MONAD_MPT_NAMESPACE_BEGIN

struct NibblesView;
struct Nibbles
{
    using size_type = uint8_t; // max length support is 255 nibbles
    bool si{false};
    size_type ei{0};
    unsigned char *data{nullptr};

    constexpr Nibbles() = default;

    Nibbles(unsigned const si_, unsigned const ei_)
        : si((si_ == ei_) ? false : (si_ & 1))
        , ei((si_ == ei_) ? 0 : static_cast<size_type>(ei_ - si_ + si))
        , data((si_ == ei_) ? nullptr : ([&] {
            MONAD_DEBUG_ASSERT(si_ <= ei_ && ei_ <= 255);
            unsigned const alloc_size = (ei + 1) / 2;
            void *ret = std::malloc(alloc_size);
            if (ret == nullptr) {
                throw std::bad_alloc();
            }
            std::memset(ret, 0, alloc_size);
            return static_cast<unsigned char *>(ret);
        }()))
    {
    }

    Nibbles &operator=(NibblesView const &other);

    ~Nibbles()
    {
        if (data) {
            std::free(data);
        }
    }

    constexpr unsigned size() const noexcept
    {
        return ((size_type)si == ei) ? 0 : ((ei + 1) / 2);
    }
};
static_assert(sizeof(Nibbles) == 16);
static_assert(alignof(Nibbles) == 8);

struct NibblesView
{
    using size_type = uint8_t; // max length support is 255 nibbles
    unsigned char const *data{nullptr};
    bool si{false};
    size_type ei{0};

    constexpr NibblesView() = default;
    constexpr NibblesView(NibblesView const &) = default;
    NibblesView &operator=(NibblesView const &) = default;

    constexpr explicit NibblesView(
        unsigned const si_, unsigned const ei_,
        unsigned char const *const data_) noexcept
        : data((si_ == ei_) ? nullptr : (data_ + si_ / 2))
        , si((si_ == ei_) ? false : (si_ & 1))
        , ei((si_ == ei_) ? 0 : static_cast<size_type>(ei_ - si_ + si))
    {
        MONAD_DEBUG_ASSERT(si_ <= ei_ && ei_ <= 255);
    }

    // constructor from byte_string_view
    constexpr NibblesView(byte_string_view const &s) noexcept
        : NibblesView(false, 2 * s.size(), s.data())
    {
    }

    // constructor from byte_string
    constexpr NibblesView(byte_string const &s) noexcept
        : NibblesView(false, 2 * s.size(), s.data())
    {
    }

    // construct from Nibbles
    constexpr NibblesView(Nibbles const &n) noexcept
        : NibblesView{n.si, n.ei, n.data}
    {
    }

    constexpr size_type nibble_size() const
    {
        return ei - static_cast<size_type>(si);
    }

    // size of data in bytes
    constexpr unsigned size() const
    {
        return (static_cast<size_type>(si) == ei) ? 0 : ((ei + 1) / 2);
    }

    constexpr NibblesView suffix(size_type pos) const
    {
        return NibblesView{
            static_cast<unsigned>(this->si + pos), this->ei, this->data};
    }

    constexpr bool operator==(NibblesView const &other) const
    {
        if (this == &other) {
            return true;
        }
        if (si != other.si || ei != other.ei) {
            return false;
        }
        if (ei == 0) {
            return true;
        }
        MONAD_DEBUG_ASSERT(data && other.data);

        for (unsigned i = si; i < ei; ++i) {
            if (get_nibble(data, i) != get_nibble(other.data, i)) {
                return false;
            }
        }
        return true;
    }

    unsigned char operator[](unsigned i) const
    {
        return get_nibble(data, si + i);
    }
};
static_assert(sizeof(NibblesView) == 16);
static_assert(alignof(NibblesView) == 8);
static_assert(std::is_trivially_copyable_v<NibblesView> == true);

inline Nibbles &Nibbles::operator=(NibblesView const &n)
{
    if (data) {
        free(data);
    }
    si = n.si;
    ei = n.ei;
    MONAD_DEBUG_ASSERT(si <= ei && ei <= 255);
    if (si == ei) {
        return *this;
    }
    unsigned const alloc_size = n.size();
    void *ret = std::malloc(alloc_size);
    if (ret == nullptr) {
        throw std::bad_alloc();
    }
    memcpy(ret, n.data, n.size());
    data = static_cast<unsigned char *>(ret);
    return *this;
}

inline Nibbles concat3(
    NibblesView const prefix, unsigned char const nibble,
    NibblesView const suffix)
{
    // calculate bytes
    unsigned ei = prefix.ei - prefix.si + 1 + suffix.ei - suffix.si;
    Nibbles res{0, ei};
    unsigned j = 0;
    for (unsigned i = prefix.si; i < prefix.ei; ++i, ++j) {
        set_nibble(res.data, j, get_nibble(prefix.data, i));
    }
    set_nibble(res.data, j++, nibble);
    for (unsigned i = suffix.si; i < suffix.ei; ++i, ++j) {
        set_nibble(res.data, j, get_nibble(suffix.data, i));
    }
    return res;
}

inline Nibbles concat2(unsigned char const nibble, NibblesView const suffix)
{
    unsigned ei = suffix.ei - suffix.si + 1;
    Nibbles res{0, ei};
    unsigned j = 0;
    set_nibble(res.data, j++, nibble);
    for (unsigned i = suffix.si; i < suffix.ei; ++i, ++j) {
        set_nibble(res.data, j, get_nibble(suffix.data, i));
    }
    return res;
}

MONAD_MPT_NAMESPACE_END