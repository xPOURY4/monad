#pragma once

#include <monad/trie/constants.hpp>
#include <monad/trie/data.hpp>
#include <monad/trie/util.hpp>

#include <monad/core/assert.h>

#include <cstddef>

MONAD_TRIE_NAMESPACE_BEGIN

struct merkle_node_t;

struct merkle_child_info_t
{
    trie_data_t noderef;
    int64_t fnext; // later change to off48_t
    merkle_node_t *next;
    unsigned char *data;
    unsigned char path_len;
    char pad[7];
    unsigned char path[32];
};

static_assert(sizeof(merkle_child_info_t) == 96);
static_assert(alignof(merkle_child_info_t) == 8);

struct merkle_node_t
{
    uint16_t mask;
    uint16_t valid_mask;
    uint16_t tomb_arr_mask;
    uint8_t nsubnodes;
    unsigned char path_len;

    merkle_child_info_t children[0];
};

static_assert(sizeof(merkle_node_t) == 8);
static_assert(alignof(merkle_node_t) == 8);

static inline uint16_t merkle_child_mask(merkle_node_t const *const node)
{
    return node->mask;
}

static inline bool
merkle_child_test(merkle_node_t const *const node, unsigned const i)
{
    uint16_t const mask = merkle_child_mask(node);
    return mask & (1u << i);
}

static inline bool merkle_child_all(merkle_node_t const *const node)
{
    uint16_t const mask = merkle_child_mask(node);
    return mask == UINT16_MAX;
}

static inline bool merkle_child_any(merkle_node_t const *const node)
{
    uint16_t const mask = merkle_child_mask(node);
    return mask;
}

static inline bool merkle_child_none(merkle_node_t const *const node)
{
    uint16_t const mask = merkle_child_mask(node);
    return !mask;
}

static inline unsigned merkle_child_count(merkle_node_t const *const node)
{
    uint16_t const mask = merkle_child_mask(node);
    return __builtin_popcount(mask);
}

static inline unsigned
merkle_child_index(merkle_node_t const *const node, unsigned const i)
{
    return child_index(node->mask, i);
}

static inline unsigned merkle_child_count_tomb(merkle_node_t const *const node)
{
    return node->nsubnodes - __builtin_popcount(node->valid_mask);
}

static inline unsigned merkle_child_count_valid(merkle_node_t const *const node)
{
    return __builtin_popcount(node->valid_mask);
}

MONAD_TRIE_NAMESPACE_END
