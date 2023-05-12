#pragma once

#include <assert.h>
#include <liburing.h>
#include <monad/mem/cpool.h>
#include <monad/tmp/node.h>
#include <monad/trie/config.h>
#include <monad/trie/data.h>
#include <monad/trie/nibble.h>
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

extern int fd;
extern struct io_uring *ring;
extern int inflight;
extern int inflight_rd;
extern int n_rd_per_block;

extern unsigned char *write_buffer;
extern size_t buffer_idx;
extern int64_t block_off;

#define SIZE_OF_CHILD_COUNT 1
#define SIZE_OF_PATH_LEN 1
#define SIZE_OF_TRIE_DATA 32
#define SIZE_OF_SUBNODE_BITMASK 2
#define SIZE_OF_FILE_OFFSET 8
#define BLOCK_TYPE_DATA 0
#define BLOCK_TYPE_META 1
#define MAX_DISK_NODE_SIZE 1416
#define CACHE_LEVELS 5

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
    uint16_t const mask = merkle_child_mask(node);
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return __builtin_popcount(mask & filter);
}

static inline unsigned merkle_child_count_tomb(merkle_node_t const *const node)
{
    return node->nsubnodes - __builtin_popcount(node->valid_mask);
}

static inline unsigned merkle_child_count_valid(merkle_node_t const *const node)
{
    return __builtin_popcount(node->valid_mask);
}

/****************************************************************/
// serial / deserialization
void serialize_node_to_buffer(unsigned char *write_pos, merkle_node_t const *);

merkle_node_t *deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char node_path_len);

// node rw
int64_t write_node(merkle_node_t *node);

uint64_t sum_data_first_word(merkle_node_t *node);

void rehash_keccak(merkle_node_t *node, trie_data_t *data);

void free_trie(merkle_node_t *);

// helper functions
void set_merkle_child_from_tmp(
    merkle_node_t *parent, uint8_t arr_idx, trie_branch_node_t const *tmp_node);

static inline size_t get_disk_node_size(merkle_node_t const *const node)
{
    size_t total_path_len = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        total_path_len +=
            (node->children[i].path_len + 1) / 2 - node->path_len / 2;
    }
    return SIZE_OF_SUBNODE_BITMASK + total_path_len +
           merkle_child_count_valid(node) *
               (SIZE_OF_TRIE_DATA + SIZE_OF_FILE_OFFSET + SIZE_OF_PATH_LEN);
}

static inline size_t get_merkle_node_size(uint8_t const nsubnodes)
{
    return sizeof(merkle_node_t) + nsubnodes * sizeof(merkle_child_info_t);
}

static inline merkle_node_t *
get_new_merkle_node(uint16_t const mask, unsigned char path_len)
{
    uint8_t const nsubnodes = __builtin_popcount(mask);
    size_t const size = get_merkle_node_size(nsubnodes);
    merkle_node_t *const new_branch = (merkle_node_t *)calloc(1, size);
    new_branch->nsubnodes = nsubnodes;
    new_branch->mask = mask;
    new_branch->valid_mask = mask;
    new_branch->path_len = path_len;
    return new_branch;
}

static inline merkle_node_t *copy_merkle_node(merkle_node_t *prev_node)
{ // only copy valid subnodes
    int nsubnodes = merkle_child_count_valid(prev_node);
    merkle_node_t *copy =
        (merkle_node_t *)calloc(1, get_merkle_node_size(nsubnodes));
    copy->mask = prev_node->valid_mask;
    copy->valid_mask = copy->mask;
    copy->path_len = prev_node->path_len;
    copy->nsubnodes = nsubnodes;
    for (int i = 0, copy_child_i = 0; i < 16; ++i) {
        if (copy->mask & 1u << i) {
            copy->children[copy_child_i] =
                prev_node->children[merkle_child_index(prev_node, i)];
            copy->children[copy_child_i++].next = NULL;
        }
    }
    return copy;
}

static inline void
connect_only_grandchild(merkle_node_t *parent, uint8_t child_idx)
{
    merkle_node_t *midnode = parent->children[child_idx].next;
    uint8_t only_child_i =
        merkle_child_index(midnode, __builtin_ctz(midnode->valid_mask));
    unsigned mid_path_len = midnode->path_len;
    memcpy(
        &parent->children[child_idx],
        &midnode->children[only_child_i],
        sizeof(merkle_child_info_t) - 32);
    memcpy(
        parent->children[child_idx].path + (mid_path_len + 1) / 2,
        midnode->children[only_child_i].path + (mid_path_len + 1) / 2,
        (midnode->children[only_child_i].path_len + 1) / 2 -
            (mid_path_len + 1) / 2);
    if (mid_path_len % 2) { // odd path_len
        set_nibble(
            parent->children[child_idx].path,
            mid_path_len,
            get_nibble(midnode->children[only_child_i].path, mid_path_len));
    }
    assert(
        parent->children[child_idx].fnext ||
        parent->children[child_idx].path_len == 64);
    assert(
        (parent->children[child_idx].next != NULL) !=
        parent->children[child_idx].path_len >= CACHE_LEVELS);
    free(midnode);
}

#ifdef __cplusplus
}
#endif
