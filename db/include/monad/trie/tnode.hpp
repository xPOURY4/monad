#pragma once

#include <monad/trie/config.hpp>

#include <monad/mem/allocators.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/node.hpp>

MONAD_TRIE_NAMESPACE_BEGIN
// helper struct: node of a upward pointing tree
struct tnode_t
{
    tnode_t *const parent;
    merkle_node_t *node;
    const uint8_t child_ni;
    const uint8_t child_idx;

private:
    int8_t npending_{0};

    void (*done_)(tnode_t *, void *) noexcept {nullptr};
    void *done_value_{nullptr};

public:
    tnode_t(
        tnode_t *const parent_, merkle_node_t *const node_,
        const uint8_t child_ni_, const uint8_t child_idx_)
        : parent(parent_)
        , node(node_)
        , child_ni(child_ni_)
        , child_idx(child_idx_)
    {
    }
    tnode_t(void (*done)(tnode_t *, void *) noexcept, void *done_value)
        : parent(nullptr)
        , node(nullptr)
        , child_ni(0)
        , child_idx(0)
        , done_(done)
        , done_value_(done_value)
    {
    }
    int8_t npending() const noexcept
    {
        return npending_;
    }
    void set_npending(int8_t v) noexcept
    {
        npending_ = v;
    }
    void decrement_npending() noexcept
    {
        assert(npending_ > 0);
        if (0 == --npending_ && done_ != nullptr) {
            done_(this, done_value_);
        }
    }

    using allocator_type = allocators::boost_unordered_pool_allocator<tnode_t>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        tnode_t, allocators::unique_ptr_allocator_deleter<
                     allocator_type, &tnode_t::pool>>;
    static unique_ptr_type make(tnode_t v)
    {
        return allocators::allocate_unique<allocator_type, &tnode_t::pool>(
            std::move(v));
    }
};
static_assert(sizeof(tnode_t) == 40);
static_assert(alignof(tnode_t) == 8);

inline tnode_t::unique_ptr_type get_new_tnode(
    tnode_t *const parent_tnode, uint8_t new_branch_ni,
    uint8_t const new_branch_arr_i, merkle_node_t *new_branch)
{
    auto branch_tnode = tnode_t::make(
        tnode_t{parent_tnode, new_branch, new_branch_ni, new_branch_arr_i});
    return branch_tnode;
}

inline tnode_t::unique_ptr_type
get_new_tnode(void (*done)(tnode_t *, void *) noexcept, void *done_value)
{
    auto branch_tnode = tnode_t::make(tnode_t{done, done_value});
    return branch_tnode;
}

MONAD_TRIE_NAMESPACE_END