#pragma once

#include <stddef.h>

static inline size_t popcount(size_t const x)
{
    return __builtin_popcountl(x);
}
