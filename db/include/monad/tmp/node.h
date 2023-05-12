#pragma once

#include <assert.h>
#include <monad/mem/cpool.h>
#include <monad/trie/data.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
{
#endif

CPOOL_DEFINE(31);
extern cpool_31_t *tmp_pool;

typedef unsigned char trie_node_type_t;

#ifndef UNKNOWN
    #define UNKNOWN 0
    #define BRANCH 1
    #define LEAF 2
#endif

/*
typedef enum trie_node_type_t
    : unsigned char
{
    UNKNOWN = 0,
    BRANCH,
    LEAF
} trie_node_type_t;
*/

// TODO: add tombstone to mark for erase
typedef struct trie_branch_node_t
{
    trie_node_type_t type;

    unsigned char path_len; // number of nibbles
    unsigned char path[32];

    char pad[6];
    trie_data_t data; // TODO: remove later

    uint32_t next[16];
    uint16_t subnode_bitmask;
    int8_t nsubnodes;
} trie_branch_node_t;

static_assert(sizeof(trie_branch_node_t) == 144);
static_assert(alignof(trie_branch_node_t) == 8);

typedef struct trie_leaf_node_t
{
    trie_node_type_t type;

    unsigned char path_len;
    unsigned char path[32];
    bool tombstone;

    char pad[5];

    trie_data_t data;
} trie_leaf_node_t;

static_assert(sizeof(trie_leaf_node_t) == 72);
static_assert(alignof(trie_leaf_node_t) == 8);

/* inline helper functions */
static inline trie_branch_node_t *get_node(uint32_t const i)
{
    return (trie_branch_node_t *)cpool_ptr31(tmp_pool, i);
}

static inline uint32_t
get_new_branch(unsigned char const *const path, unsigned char const path_len)
{
    uint32_t branch_i = cpool_reserve31(tmp_pool, sizeof(trie_branch_node_t));
    cpool_advance31(tmp_pool, sizeof(trie_branch_node_t));
    // allocate the next spot for branch
    trie_branch_node_t *branch =
        (trie_branch_node_t *)cpool_ptr31(tmp_pool, branch_i);
    memset(branch, 0, sizeof(trie_branch_node_t));
    branch->type = BRANCH;
    branch->path_len = path_len;
    memcpy(branch->path, path, (path_len + 1) / 2);
    return branch_i;
}

static inline uint32_t get_new_leaf(
    unsigned char const *const path, unsigned char const path_len,
    trie_data_t const *const data, bool tombstone)
{
    uint32_t leaf_i = cpool_reserve31(tmp_pool, sizeof(trie_leaf_node_t));
    cpool_advance31(tmp_pool, sizeof(trie_leaf_node_t));
    trie_leaf_node_t *leaf = (trie_leaf_node_t *)cpool_ptr31(tmp_pool, leaf_i);
    memset(leaf, 0, sizeof(trie_leaf_node_t));
    leaf->type = LEAF;
    leaf->path_len = path_len;
    memcpy(leaf->path, path, (path_len + 1) / 2);
    copy_trie_data(&leaf->data, data);
    leaf->tombstone = tombstone;
    return leaf_i;
}

#ifdef __cplusplus
}
#endif