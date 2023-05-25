#pragma once

#include <monad/trie/encode_node.hpp>
#include <monad/trie/merge_data.hpp>
#include <monad/trie/node.hpp>
#include <monad/trie/node_helper.hpp>
#include <monad/trie/tmp_trie.hpp>
#include <monad/trie/tnode.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

merkle_node_t *do_merge(
    merkle_node_t *const prev_root, tmp_branch_node_t const *const tmp_root,
    unsigned char const pi, tnode_t *curr_tnode, AsyncIO &io_);

void merge_trie(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    tmp_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode, AsyncIO &io_);

void upward_update_data(tnode_t *curr_tnode, AsyncIO &io_);

void set_merkle_child_from_tmp(
    merkle_node_t *const parent, uint8_t const arr_idx,
    tmp_branch_node_t const *const tmp_node, AsyncIO &io_);

// must align with read callback function interface
void merge_callback(void *user_data, AsyncIO &io_);

MONAD_TRIE_NAMESPACE_END