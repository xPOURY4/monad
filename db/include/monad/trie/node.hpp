#pragma once

#include <monad/core/assert.h>
#include <monad/trie/constants.hpp>
#include <monad/trie/util.hpp>

#include <cstddef>
#include <type_traits>

MONAD_TRIE_NAMESPACE_BEGIN

struct merkle_node_t;

// maintain lifetime of data_view.data()
struct merkle_child_info_t
{
    unsigned char noderef[32];
    int64_t fnext; // TODO: change to off48_t
    merkle_node_t *next;
    unsigned char *data;
    unsigned char data_len;
    unsigned char path_len;
    char pad[6];
    unsigned char path[32];
};

static_assert(sizeof(merkle_child_info_t) == 96);
static_assert(alignof(merkle_child_info_t) == 8);
static_assert(std::is_trivially_copyable_v<merkle_child_info_t>);

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
static_assert(std::is_trivially_copyable_v<merkle_node_t>);

inline uint16_t merkle_child_mask(merkle_node_t const *const node) noexcept
{
    return node->mask;
}

inline bool
merkle_child_test(merkle_node_t const *const node, unsigned const i) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask & (1u << i);
}

inline bool merkle_child_all(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask == UINT16_MAX;
}

inline bool merkle_child_any(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return mask;
}

inline bool merkle_child_none(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return !mask;
}

inline unsigned merkle_child_count(merkle_node_t const *const node) noexcept
{
    uint16_t const mask = merkle_child_mask(node);
    return std::popcount(mask);
}

inline unsigned
merkle_child_index(merkle_node_t const *const node, unsigned const i) noexcept
{
    return child_index(node->mask, i);
}

inline unsigned
merkle_child_count_tomb(merkle_node_t const *const node) noexcept
{
    return node->nsubnodes - std::popcount(node->valid_mask);
}

inline unsigned
merkle_child_count_valid(merkle_node_t const *const node) noexcept
{
    return std::popcount(node->valid_mask);
}

inline unsigned char
partial_path_len(merkle_node_t const *const parent, unsigned const i) noexcept
{
    return parent->children[i].path_len - parent->path_len - 1;
}

MONAD_TRIE_NAMESPACE_END
