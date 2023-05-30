#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/globals.hpp>
#include <monad/trie/node.hpp>
#include <monad/trie/tmp_trie.hpp>
#include <monad/trie/tnode.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

// forward async stuff
class AsyncIO;
enum class uring_data_type_t : unsigned char;

struct merge_uring_data_t
{
    uring_data_type_t rw_flag;
    char pad[7];
    // read buffer
    unsigned char *buffer;
    int64_t offset;
    // params
    merkle_node_t *prev_parent;
    tmp_branch_node_t const *tmp_parent;
    merkle_node_t *new_parent;
    tnode_t *parent_tnode;
    // read buffer starting offset
    int16_t buffer_off;
    unsigned char pi;
    uint8_t prev_child_i;
    uint8_t tmp_branch_i;
    uint8_t new_child_ni;
};

static_assert(sizeof(merge_uring_data_t) == 64);
static_assert(alignof(merge_uring_data_t) == 8);

static inline merge_uring_data_t *get_merge_uring_data(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    tmp_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode)
{
    MONAD_TRIE_ASSERT(prev_parent->children[prev_child_i].path_len < 64);

    merge_uring_data_t *user_data = (merge_uring_data_t *)cpool_ptr29(
        tmppool_, cpool_reserve29(tmppool_, sizeof(merge_uring_data_t)));
    cpool_advance29(tmppool_, sizeof(merge_uring_data_t));

    // prep uring data
    int64_t node_offset = prev_parent->children[prev_child_i].fnext;
    int64_t offset = (node_offset >> 9) << 9;
    int16_t buffer_off = node_offset - offset;

    merge_uring_data_t tmp_data{
        .rw_flag = uring_data_type_t::IS_READ,
        .pad = {},
        .buffer = 0,
        .offset = offset,
        .prev_parent = prev_parent,
        .tmp_parent = tmp_parent,
        .new_parent = new_parent,
        .parent_tnode = parent_tnode,
        .buffer_off = buffer_off,
        .pi = pi,
        .prev_child_i = prev_child_i,
        .tmp_branch_i = tmp_branch_i,
        .new_child_ni = new_child_ni,
    };

    *user_data = tmp_data;
    return user_data;
}

MONAD_TRIE_NAMESPACE_END