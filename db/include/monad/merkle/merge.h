#pragma once

#include <monad/merkle/node.h>
#include <monad/merkle/tnode.h>

#ifdef __cplusplus
extern "C"
{
#endif

// TODO: add erase with tombstone
merkle_node_t *do_merge(
    merkle_node_t *prev_root, trie_branch_node_t const *tmp_root,
    unsigned char pi, tnode_t *curr);

void merge_trie(
    merkle_node_t *prev_parent, uint8_t prev_child_i,
    trie_branch_node_t const *tmp_parent, uint8_t tmp_branch_i,
    unsigned char pi, merkle_node_t *new_parent, uint8_t new_branch_arr_i,
    tnode_t *parent);

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
    uint8_t const prev_child_i;
    uint8_t const tmp_branch_i;
    uint8_t const new_branch_arr_i;
    tnode_t *parent;
} merge_uring_data_t;

static_assert(sizeof(merge_uring_data_t) == 48);
static_assert(alignof(merge_uring_data_t) == 8);

static inline merge_uring_data_t *get_merge_uring_data(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    trie_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_branch_arr_i, tnode_t *parent)
{
    merge_uring_data_t *user_data = (merge_uring_data_t *)cpool_ptr31(
        &tmp_pool, cpool_reserve31(&tmp_pool, sizeof(merge_uring_data_t)));
    cpool_advance31(&tmp_pool, sizeof(merge_uring_data_t));
    merge_uring_data_t tmp_data = (merge_uring_data_t){
        .prev_parent = prev_parent,
        .tmp_parent = tmp_parent,
        .new_parent = new_parent,
        .pi = pi,
        .prev_child_i = prev_child_i,
        .tmp_branch_i = tmp_branch_i,
        .new_branch_arr_i = new_branch_arr_i,
        .parent = parent,
    };
    memcpy(user_data, &tmp_data, sizeof(merge_uring_data_t));
    return user_data;
}

void upward_update_data(tnode_t *curr_tnode);

// submit async read
void async_read_request(merge_uring_data_t *merge_params);

void poll_uring();

#ifdef __cplusplus
}
#endif
