#pragma once

#include <limits.h>
#include <stddef.h>

#ifndef SIZE_WIDTH
    #define SIZE_WIDTH (sizeof(size_t) * CHAR_BIT) /* TODO c23 */
#endif

static inline size_t max(size_t const x, size_t const y)
{
    return x > y ? x : y;
}

/**
 * undefined if n is 0 or 1
 */
static inline size_t round_pow2(size_t const n)
{
    return SIZE_WIDTH - __builtin_clzl(n - 1);
}

static inline size_t round_up(size_t const x, size_t const y)
{
    size_t z = x + (y - 1);
    z /= y;
    z *= y;
    return z;
}
