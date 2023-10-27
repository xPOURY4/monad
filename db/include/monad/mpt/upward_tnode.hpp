#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/mem/allocators.hpp>

#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/util.hpp>

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

struct ChildData;
class Node;

struct UpwardTreeNode
{
    UpwardTreeNode *parent{nullptr};
    Node *node{nullptr}; // new node
    node_ptr old{}; // tnode owns old node's lifetime only when old is leaf
                    // node, as opt_leaf_data has to be valid in memory when it
                    // works the way back to recompute leaf data
    allocators::owning_span<ChildData> children{};
    Nibbles relpath{};
    std::optional<byte_string_view> opt_leaf_data{std::nullopt};
    uint16_t mask{0};
    uint16_t orig_mask{0};
    uint8_t child_branch_bit{INVALID_BRANCH};
    int8_t npending{0};
    uint8_t trie_section{0}; // max 255 diff sections in trie
    uint8_t pi{0};

    // void (*done_)(UpwardTreeNode *, void *) noexcept {nullptr};
    // void *done_value_{nullptr};

    void init(
        uint16_t const _mask, unsigned const _pi,
        std::optional<byte_string_view> const _opt_leaf_data = std::nullopt)
    {
        unsigned const n = bitmask_count(_mask);
        mask = _mask;
        orig_mask = _mask;
        npending = n;
        pi = static_cast<uint8_t>(_pi);
        children = allocators::owning_span<ChildData>(n);
        opt_leaf_data = _opt_leaf_data;
    }

    void link_parent(
        UpwardTreeNode *const parent_tnode, uint8_t const branch) noexcept
    {
        parent = parent_tnode;
        child_branch_bit = branch;
    }

    constexpr uint8_t child_j() const noexcept
    {
        MONAD_ASSERT(parent != nullptr);
        return bitmask_index(parent->orig_mask, child_branch_bit);
    }
    using allocator_type =
        allocators::boost_unordered_pool_allocator<UpwardTreeNode>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        UpwardTreeNode, allocators::unique_ptr_allocator_deleter<
                            allocator_type, &UpwardTreeNode::pool>>;
    static unique_ptr_type make(UpwardTreeNode v)
    {
        return allocators::
            allocate_unique<allocator_type, &UpwardTreeNode::pool>(
                std::move(v));
    }
};
using tnode_unique_ptr = UpwardTreeNode::unique_ptr_type;

inline tnode_unique_ptr
make_tnode(uint8_t const trie_section = 0, node_ptr old = {})
{
    return UpwardTreeNode::make(
        UpwardTreeNode{.old = std::move(old), .trie_section = trie_section});
}

static_assert(sizeof(UpwardTreeNode) == 88);
static_assert(alignof(UpwardTreeNode) == 8);

MONAD_MPT_NAMESPACE_END