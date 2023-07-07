#pragma once

#include <monad/trie/config.hpp>

#include <monad/trie/allocators.hpp>
#include <monad/trie/node.hpp>

MONAD_TRIE_NAMESPACE_BEGIN
// helper struct: node of a upward pointing tree
struct tnode_t
{
    tnode_t *parent;
    merkle_node_t *node;
    int8_t npending;
    uint8_t child_ni;
    uint8_t child_idx;

    using allocator_type = boost_unordered_pool_allocator<tnode_t>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        tnode_t, unique_ptr_allocator_deleter<allocator_type, &tnode_t::pool>>;
    static unique_ptr_type make(tnode_t v)
    {
        return allocate_unique<allocator_type, &tnode_t::pool>(std::move(v));
    }
};
static_assert(sizeof(tnode_t) == 24);
static_assert(alignof(tnode_t) == 8);

inline tnode_t::unique_ptr_type get_new_tnode(
    tnode_t *const parent_tnode, uint8_t new_branch_ni,
    uint8_t const new_branch_arr_i, merkle_node_t *new_branch)
{ // no npending
    auto branch_tnode = tnode_t::make(tnode_t{
        .parent = parent_tnode,
        .node = new_branch,
        .child_ni = new_branch_ni,
        .child_idx = new_branch_arr_i});

    return branch_tnode;
}

MONAD_TRIE_NAMESPACE_END