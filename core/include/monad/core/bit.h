#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef SIZE_WIDTH
    #define SIZE_WIDTH (sizeof(size_t) * CHAR_BIT) /* TODO c23 */
#endif

static inline size_t popcount(size_t const x)
{
    return __builtin_popcountl(x);
}

static inline bool has_single_bit(size_t const x)
{
    return popcount(x) == 1;
}

/**
 * undefined if x = 0
 */
static inline size_t countl_zero_undef(size_t const x)
{
    return __builtin_clzl(x);
}

/**
 * undefined if x = 0
 */
static inline size_t countr_zero_undef(size_t const x)
{
    return __builtin_ctzl(x);
}

/**
 * undefined if x = 0
 */
static inline size_t bit_width_undef(size_t const x)
{
    return SIZE_WIDTH - countl_zero_undef(x);
}

/**
 * undefined if x <= 1
 */
static inline size_t bit_ceil_undef(size_t const x)
{
    return 1UL << bit_width_undef(x - 1);
}
