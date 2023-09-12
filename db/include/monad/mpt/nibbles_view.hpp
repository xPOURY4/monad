#pragma once

#include <monad/core/assert.h>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

struct NibblesView
{
    unsigned char const *const data{nullptr};
    bool const si{false};
    uint8_t const ei{0};

    constexpr unsigned size() const
    {
        if ((uint8_t)si == ei) {
            return 0;
        }
        return (ei + 1) / 2;
    }

    constexpr NibblesView() = default;

    constexpr explicit NibblesView(
        unsigned si_, unsigned ei_, unsigned char const *data_)
        : data((si_ == ei_) ? nullptr : (data_ + si_ / 2))
        , si((si_ == ei_) ? false : si_ & 1)
        , ei((si_ == ei_) ? 0 : (ei_ - si_ + si))
    {
        MONAD_DEBUG_ASSERT(si_ <= ei_);
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
};
static_assert(sizeof(NibblesView) == 16);
static_assert(alignof(NibblesView) == 8);

MONAD_MPT_NAMESPACE_END