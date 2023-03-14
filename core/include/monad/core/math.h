#pragma once

#include <stddef.h>

static inline size_t max(size_t const x, size_t const y)
{
    return x > y ? x : y;
}

/**
 * returns smallest z such that z % y == 0 and z >= x
 */
static inline size_t round_up(size_t const x, size_t const y)
{
    size_t z = x + (y - 1);
    z /= y;
    z *= y;
    return z;
}
