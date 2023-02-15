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

    unsigned char data[32];

    char pad[6];

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

    unsigned char data[32];
} trie_leaf_node_t;

static_assert(sizeof(trie_leaf_node_t) == 66);
static_assert(alignof(trie_leaf_node_t) == 1);

#ifdef __cplusplus
}
#endif
