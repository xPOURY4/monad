#pragma once

#include <monad/trie/config.hpp>

#include <assert.h>
#include <stdint.h>

MONAD_TRIE_NAMESPACE_BEGIN

typedef union trie_data_t
{
    unsigned char bytes[32];
    uint64_t words[4];
} trie_data_t;

static_assert(sizeof(trie_data_t) == 32);
static_assert(alignof(trie_data_t) == 8);

MONAD_TRIE_NAMESPACE_END