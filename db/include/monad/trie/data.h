#pragma once

#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

static_assert(CHAR_BIT == 8);

typedef union trie_data_t
{
    unsigned char bytes[32];
    uint64_t words[4];
} trie_data_t;

static_assert(sizeof(trie_data_t) == 32);
static_assert(alignof(trie_data_t) == 8);

#ifdef __cplusplus
}
#endif
