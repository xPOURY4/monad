#pragma once

#include <monad/core/assert.h>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>

#include <cstdint>
#include <cstdlib>
#include <cstring>

MONAD_MPT_NAMESPACE_BEGIN

struct NibblesView;
struct Nibbles
{
    bool si{false};
    uint8_t ei{0};
    unsigned char *data{nullptr};

    constexpr Nibbles() = default;

    Nibbles(unsigned const si_, unsigned const ei_)
        : si((si_ == ei_) ? false : (si_ & 1))
        , ei((si_ == ei_) ? 0 : static_cast<uint8_t>(ei_ - si_ + si))
        , data((si_ == ei_) ? nullptr : ([&] {
            MONAD_DEBUG_ASSERT(si_ <= ei_ && ei_ <= 128);
            unsigned const alloc_size = (ei + 1) / 2;
            auto *ret = std::malloc(alloc_size);
            if (ret == nullptr) {
                throw std::bad_alloc();
            }
            std::memset(ret, 0, alloc_size);
            return reinterpret_cast<unsigned char *>(ret);
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

    constexpr unsigned size() const
    {
        return ((uint8_t)si == ei) ? 0 : ((ei + 1) / 2);
    }
};
static_assert(sizeof(Nibbles) == 16);
static_assert(alignof(Nibbles) == 8);

struct NibblesView
{
    unsigned char const *data{nullptr};
    bool si{false};
    uint8_t ei{0};

    constexpr unsigned size() const
    {
        return (static_cast<uint8_t>(si) == ei) ? 0 : ((ei + 1) / 2);
    }

    constexpr NibblesView() = default;

    constexpr explicit NibblesView(
        unsigned const si_, unsigned const ei_,
        unsigned char const *const data_)
        : data((si_ == ei_) ? nullptr : (data_ + si_ / 2))
        , si((si_ == ei_) ? false : (si_ & 1))
        , ei((si_ == ei_) ? 0 : static_cast<uint8_t>(ei_ - si_ + si))
    {
        MONAD_DEBUG_ASSERT(si_ <= ei_ && ei_ <= 128);
    }

    constexpr NibblesView(Nibbles const &n)
        : NibblesView{n.si, n.ei, n.data}
    {
    }

    constexpr NibblesView(NibblesView const &other) = default;
    NibblesView &operator=(NibblesView const &other) = default;

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
};
static_assert(sizeof(NibblesView) == 16);
static_assert(alignof(NibblesView) == 8);

inline Nibbles &Nibbles::operator=(NibblesView const &n)
{
    if (data) {
        free(data);
    }
    si = n.si;
    ei = n.ei;
    MONAD_DEBUG_ASSERT(si <= ei && ei <= 128);
    if (si == ei) {
        return *this;
    }
    unsigned const alloc_size = n.size();
    auto *ret = std::malloc(alloc_size);
    if (ret == nullptr) {
        throw std::bad_alloc();
    }
    memcpy(ret, n.data, n.size());
    data = reinterpret_cast<unsigned char *>(ret);
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