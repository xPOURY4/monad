#pragma once

#include <utils/assert.h>

#include <limits>
#include <type_traits>

namespace monad::runtime
{
    template <typename Int>
    constexpr Int saturating_add(Int x, Int y)
        requires(std::is_integral_v<Int> && std::is_unsigned_v<Int>)
    {
        Int sum;

        if (MONAD_COMPILER_UNLIKELY(__builtin_add_overflow(x, y, &sum))) {
            return std::numeric_limits<Int>::max();
        }

        return sum;
    }

    template <typename Int>
    constexpr Int saturating_sub(Int x, Int y)
        requires(std::is_integral_v<Int> && std::is_unsigned_v<Int>)
    {
        Int result;

        if (MONAD_COMPILER_UNLIKELY(__builtin_sub_overflow(x, y, &result))) {
            return 0;
        }

        return result;
    }

    template <typename To, typename From>
    constexpr To clamp_cast(From x) noexcept
    {
        if (x > std::numeric_limits<To>::max()) {
            return std::numeric_limits<To>::max();
        }

        return static_cast<To>(x);
    }
}
