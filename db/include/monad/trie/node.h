#pragma once

#include <monad/trie/config.h>
#include <monad/trie/data.h>

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum trie_node_type_t
    : unsigned char
{
    UNKNOWN = 0,
    BRANCH,
    LEAF
} trie_node_type_t;

typedef struct trie_branch_node_t
{
    trie_node_type_t type;

    unsigned char prefix_len;
    unsigned char prefix[32];

    char pad[6];

    trie_data_t data;

    unsigned char *next[16];
    int64_t fnext[16];
} trie_branch_node_t;

static_assert(sizeof(trie_branch_node_t) == 328);
static_assert(alignof(trie_branch_node_t) == 8);

typedef struct trie_leaf_node_t
{
    trie_node_type_t type;

    unsigned char prefix_len;
    unsigned char prefix[32];

    char pad[6];

    trie_data_t data;
} trie_leaf_node_t;

static_assert(sizeof(trie_leaf_node_t) == 72);
static_assert(alignof(trie_leaf_node_t) == 8);

#ifdef __cplusplus
}
#endif
