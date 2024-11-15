#pragma once

#include <utils/assert.h>

#include <limits>
#include <type_traits>

namespace monad::runtime
{
    template <typename Int>
    Int saturating_add(Int x, Int y)
        requires(std::is_integral_v<Int> && std::is_unsigned_v<Int>)
    {
        Int sum;

        if (MONAD_COMPILER_UNLIKELY(__builtin_add_overflow(x, y, &sum))) {
            return std::numeric_limits<Int>::max();
        }

        return sum;
    }
}
