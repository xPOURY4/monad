#pragma once

#include <monad/trie/node.hpp>
#include <stdint.h>

MONAD_TRIE_NAMESPACE_BEGIN
// helper struct: node of a upward pointing tree
struct tnode_t
{
    tnode_t *parent;
    merkle_node_t *node;
    int8_t npending;
    uint8_t child_ni;
    uint8_t child_idx;

    tnode_t() = delete;
    ~tnode_t() = delete;
};

static_assert(sizeof(tnode_t) == 24);
static_assert(alignof(tnode_t) == 8);

static inline tnode_t *get_new_tnode(
    tnode_t *const parent_tnode, uint8_t new_branch_ni,
    uint8_t const new_branch_arr_i, merkle_node_t *const new_branch)
{ // no npending
    tnode_t *const branch_tnode = (tnode_t *)cpool_ptr29(
        trie_pool_, cpool_reserve29(trie_pool_, sizeof(tnode_t)));
    cpool_advance29(trie_pool_, sizeof(tnode_t));

    branch_tnode->node = new_branch;
    branch_tnode->parent = parent_tnode;
    branch_tnode->child_ni = new_branch_ni;
    branch_tnode->child_idx = new_branch_arr_i;

    return branch_tnode;
}

MONAD_TRIE_NAMESPACE_END