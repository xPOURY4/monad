#pragma once

#include <monad/trie/config.hpp>

#include <monad/trie/allocators.hpp>

MONAD_TRIE_NAMESPACE_BEGIN
// helper struct: node of a upward pointing tree
struct merkle_node_t;
struct tnode_t
{
    tnode_t *parent;
    merkle_node_t *node;
    int8_t npending;
    uint8_t child_ni;
    uint8_t child_idx;

    using allocator_type = boost_object_pool_allocator<tnode_t>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        tnode_t, unique_ptr_allocator_deleter<allocator_type, &tnode_t::pool>>;
    static unique_ptr_type make()
    {
        return allocate_unique<allocator_type, &tnode_t::pool>();
    }
};
static_assert(sizeof(tnode_t) == 24);
static_assert(alignof(tnode_t) == 8);

static inline tnode_t::unique_ptr_type get_new_tnode(
    tnode_t *const parent_tnode, uint8_t new_branch_ni,
    uint8_t const new_branch_arr_i, merkle_node_t *const new_branch)
{ // no npending
    auto branch_tnode = tnode_t::make();
    branch_tnode->node = new_branch;
    branch_tnode->parent = parent_tnode;
    branch_tnode->child_ni = new_branch_ni;
    branch_tnode->child_idx = new_branch_arr_i;

    return branch_tnode;
}

MONAD_TRIE_NAMESPACE_END