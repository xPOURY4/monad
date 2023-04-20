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

#ifdef __cplusplus
}
#endif
