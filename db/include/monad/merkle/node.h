#pragma once

#include <assert.h>
#include <monad/mem/cpool.h>
#include <monad/tmp/node.h>
#include <monad/trie/config.h>
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

#define BUFFER_SIZE 64 * 1024
#define SIZE_OF_CHILD_COUNT 1
#define SIZE_OF_PATH_LEN 1
#define SIZE_OF_TRIE_DATA 32
#define SIZE_OF_SUBNODE_BITMASK 2
#define SIZE_OF_FILE_OFFSET 8
#define BLOCK_TYPE_DATA 0
#define BLOCK_TYPE_META 1
#define ALIGNMENT 512

// TODO: get rid of mempool
extern cpool_31_t pool;
extern int fd;

typedef struct merkle_child_info_t merkle_child_info_t;
typedef struct merkle_node_t merkle_node_t;

struct merkle_child_info_t
{
    trie_data_t data;
    int64_t fnext;
    merkle_node_t *next;
    unsigned char path_len;
    char pad[7];
    unsigned char path[32];
};

static_assert(sizeof(merkle_child_info_t) == 88);
static_assert(alignof(merkle_child_info_t) == 8);

struct merkle_node_t
{
    uint16_t mask;
    uint8_t nsubnodes;

    char pad[5];

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
    uint16_t const mask = merkle_child_mask(node);
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return __builtin_popcount(mask & filter);
}

/****************************************************************/

// helper function
void set_merkle_child(
    merkle_node_t *parent, uint8_t arr_idx, trie_branch_node_t const *tmp_node);

merkle_node_t *copy_tmp_trie(trie_branch_node_t const *node, uint16_t mask);

unsigned char *
write_node_to_buffer(unsigned char *write_pos, merkle_node_t const *);

static inline size_t get_disk_node_size(merkle_node_t const *const node)
{
    size_t total_path_len = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        total_path_len += (node->children[i].path_len + 1) / 2;
    }
    return SIZE_OF_SUBNODE_BITMASK + SIZE_OF_CHILD_COUNT + total_path_len +
           node->nsubnodes *
               (SIZE_OF_TRIE_DATA + SIZE_OF_FILE_OFFSET + SIZE_OF_PATH_LEN);
}

static inline size_t get_merkle_node_size(uint8_t const nsubnodes)
{
    return sizeof(merkle_node_t) + nsubnodes * sizeof(merkle_child_info_t);
}

static inline merkle_node_t *
get_merkle_next(merkle_node_t const *const node, unsigned int const child_idx)
{
    // node->children[child_idx].next = read_node_from_disk(fd, f_off);
    return node->children[child_idx].next;
}

static inline merkle_node_t *get_new_merkle_node(uint16_t const mask)
{
    uint8_t const nsubnodes = __builtin_popcount(mask);
    size_t const size = get_merkle_node_size(nsubnodes);
    merkle_node_t *const new_branch = (merkle_node_t *)calloc(1, size);
    new_branch->nsubnodes = nsubnodes;
    new_branch->mask = mask;
    return new_branch;
}

static inline merkle_node_t *copy_merkle_node(merkle_node_t *node)
{
    size_t const size = get_merkle_node_size(node->nsubnodes);
    merkle_node_t *const copied_node = (merkle_node_t *)malloc(size);
    memcpy(copied_node, node, size);
    return copied_node;
}

#ifdef __cplusplus
}
#endif
