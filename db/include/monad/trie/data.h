#pragma once

#include <monad/trie/config.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef union trie_data_t
{
    unsigned char bytes[32];
    uint64_t words[4];
} trie_data_t;

void copy_trie_data(trie_data_t *dest_data, trie_data_t *src_data);
bool cmp_trie_data(trie_data_t *dest_data, trie_data_t *src_data);

static_assert(sizeof(trie_data_t) == 32);
static_assert(alignof(trie_data_t) == 8);

#ifdef __cplusplus
}
#endif
