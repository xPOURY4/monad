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
    unsigned npending{0};
    uint8_t trie_section{0}; // max 255 diff sections in trie
    uint8_t prefix_index{0};

    // void (*done_)(UpwardTreeNode *, void *) noexcept {nullptr};
    // void *done_value_{nullptr};

    void init(
        uint16_t const mask_, unsigned const pi_,
        std::optional<byte_string_view> const opt_leaf_data_ = std::nullopt)
    {
        mask = mask_;
        orig_mask = mask_;
        npending = number_of_children();
        prefix_index = static_cast<uint8_t>(pi_);
        children = allocators::owning_span<ChildData>(number_of_children());
        opt_leaf_data = opt_leaf_data_;
    }

    [[nodiscard]] unsigned number_of_children() const
    {
        return static_cast<unsigned>(std::popcount(mask));
    }

    constexpr uint8_t child_index() const noexcept
    {
        MONAD_ASSERT(parent != nullptr);
        return static_cast<uint8_t>(
            bitmask_index(parent->orig_mask, child_branch_bit));
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

inline tnode_unique_ptr make_tnode(
    uint8_t const trie_section = 0, UpwardTreeNode *const parent = nullptr,
    uint8_t const child_branch_bit = 0, node_ptr old = {})
{
    // tnode is linked to parent tnode on creation
    return UpwardTreeNode::make(UpwardTreeNode{
        .parent = parent,
        .node = nullptr,
        .old = std::move(old),
        .children = {},
        .relpath = {},
        .opt_leaf_data = std::nullopt,
        .mask = 0,
        .orig_mask = 0,
        .child_branch_bit = child_branch_bit,
        .trie_section = trie_section,
        .prefix_index = 0});
}

static_assert(sizeof(UpwardTreeNode) == 96);
static_assert(alignof(UpwardTreeNode) == 8);

MONAD_MPT_NAMESPACE_END
