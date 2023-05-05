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

typedef unsigned char uring_data_type_t;

#define IS_READ 0
#define IS_WRITE 1

/*
typedef enum uring_data_type_t
    : unsigned char
{
    IS_READ = 0,
    IS_WRITE
} uring_data_type_t;
*/

// async stuff
typedef struct merge_uring_data_t
{
    uring_data_type_t rw_flag;
    char pad[7];
    // read buffer
    unsigned char *buffer;
    // params
    merkle_node_t *prev_parent;
    trie_branch_node_t const *tmp_parent;
    merkle_node_t *new_parent;
    tnode_t *parent;
    // read buffer starting offset
    uint16_t buffer_off;
    unsigned char pi;
    uint8_t prev_child_i;
    uint8_t tmp_branch_i;
    uint8_t new_branch_arr_i;
} merge_uring_data_t;

static_assert(sizeof(merge_uring_data_t) == 56);
static_assert(alignof(merge_uring_data_t) == 8);

typedef struct write_uring_data_t
{
    uring_data_type_t rw_flag;
    char pad[7];
    unsigned char *buffer;
} write_uring_data_t;

static_assert(sizeof(write_uring_data_t) == 16);
static_assert(alignof(write_uring_data_t) == 8);

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
        .rw_flag = IS_READ,
        .pad = {},
        .buffer = 0,
        .prev_parent = prev_parent,
        .tmp_parent = tmp_parent,
        .new_parent = new_parent,
        .parent = parent,
        .buffer_off = 0,
        .pi = pi,
        .prev_child_i = prev_child_i,
        .tmp_branch_i = tmp_branch_i,
        .new_branch_arr_i = new_branch_arr_i,
    };
    *user_data = tmp_data;
    /*memcpy(user_data, &tmp_data, sizeof(merge_uring_data_t));*/
    return user_data;
}

void upward_update_data(tnode_t *curr_tnode);

// submit async requests
void async_read_request(merge_uring_data_t *merge_params);
void async_write_request(unsigned char *buffer, unsigned long long offset);

void poll_uring();

#ifdef __cplusplus
}
#endif
