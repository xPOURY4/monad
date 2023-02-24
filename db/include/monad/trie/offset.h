#pragma once

#include <monad/trie/config.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

typedef struct off48_t
{
    alignas(2) unsigned char a[6];
} off48_t;

static_assert(sizeof(off48_t) == 6);
static_assert(alignof(off48_t) == 2);

static inline off48_t off48_from_int(int64_t const offset)
{
    off48_t result;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&result, &offset, 6);
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    memcpy(&result, (char const *)offset + 2, 6);
#else
    #error "invalid byte order"
#endif
    return result;
}

static inline int64_t off48_to_int(off48_t const offset)
{
    int64_t result = 0;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    memcpy(&result, &offset, 6);
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    memcpy((char *)(&result) + 2, &offset, 6);
#else
    #error "invalid byte order"
#endif
    return result;
}
