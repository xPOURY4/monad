#pragma once

#include <monad/merkle/node.h>

#ifdef __cplusplus
extern "C"
{
#endif

// TODO: add erase with tombstone
merkle_node_t *do_merge(
    merkle_node_t *prev_root, trie_branch_node_t const *tmp_root,
    unsigned char pi);

void merge_trie(
    merkle_node_t *prev_parent, uint8_t prev_branch_i,
    trie_branch_node_t const *tmp_parent, uint8_t tmp_branch_i,
    unsigned char pi, merkle_node_t *new_parent, uint8_t new_branch_arr_i);

// async stuff
typedef struct merge_uring_data_t
{
    // read buffer
    unsigned char *buffer;
    // params
    merkle_node_t *const prev_parent;
    trie_branch_node_t const *const tmp_parent;
    merkle_node_t *const new_parent;
    // read buffer starting offset
    unsigned buffer_off;
    unsigned char const pi;
    uint8_t const prev_branch_i;
    uint8_t const tmp_branch_i;
    uint8_t const new_branch_arr_i;
} merge_uring_data_t;

static_assert(sizeof(merge_uring_data_t) == 40);
static_assert(alignof(merge_uring_data_t) == 8);

static inline merge_uring_data_t *get_merge_uring_data(
    merkle_node_t *const prev_parent, uint8_t const prev_branch_i,
    trie_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_branch_arr_i)
{
    merge_uring_data_t *user_data =
        (merge_uring_data_t *)malloc(sizeof(merge_uring_data_t));
    merge_uring_data_t tmp_data = (merge_uring_data_t){
        .prev_parent = prev_parent,
        .tmp_parent = tmp_parent,
        .new_parent = new_parent,
        .pi = pi,
        .prev_branch_i = prev_branch_i,
        .tmp_branch_i = tmp_branch_i,
        .new_branch_arr_i = new_branch_arr_i,
    };
    memcpy(user_data, &tmp_data, sizeof(merge_uring_data_t));
    return user_data;
}

merkle_node_t *async_get_merkle_next(
    merkle_node_t *const node, unsigned const child_idx,
    merge_uring_data_t *const merge_params);

// submit async read
void async_read_node_from_disk(merge_uring_data_t *merge_params);

void poll_uring();

#ifdef __cplusplus
}
#endif
