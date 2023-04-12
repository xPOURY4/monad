#pragma once

#include <assert.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

struct merkle_node_t
{
    char mask[2];
    char path_off[0];
    char data_off[0];
    char path[0];
    char data[0];
};
typedef struct merkle_node_t merkle_node_t;

static_assert(sizeof(merkle_node_t) == 2);
static_assert(alignof(merkle_node_t) == 1);

static inline uint16_t merkle_child_mask(merkle_node_t const *const node)
{
    return *(uint16_t const *)(node->mask);
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
    uint16_t const mask = merkle_child_mask(node);
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return __builtin_popcount(mask & filter);
}
