#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/mem/allocators.hpp>

#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/util.hpp>

#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

struct UpwardTreeNode
{
    UpwardTreeNode *parent{nullptr};
    // old only exists to extend the lifetime of opt_leaf_data, when the data
    // comes from old
    Node::UniquePtr old{};
    allocators::owning_span<ChildData> children{};
    Nibbles path{};
    std::optional<byte_string_view> opt_leaf_data{std::nullopt};
    uint16_t mask{0};
    uint16_t orig_mask{0};
    uint8_t branch{INVALID_BRANCH};
    uint8_t npending{0};
    uint8_t trie_section{0}; // max 255 diff sections in trie
    uint8_t prefix_index{0};

    [[nodiscard]] unsigned number_of_children() const
    {
        return static_cast<unsigned>(std::popcount(mask));
    }

    constexpr uint8_t child_index() const noexcept
    {
        MONAD_ASSERT(parent != nullptr);
        return static_cast<uint8_t>(bitmask_index(parent->orig_mask, branch));
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
    uint16_t const orig_mask, unsigned const prefix_index,
    uint8_t const trie_section, UpwardTreeNode *const parent = nullptr,
    uint8_t const branch = INVALID_BRANCH, NibblesView const path = {},
    std::optional<byte_string_view> const opt_leaf_data = std::nullopt,
    Node::UniquePtr old = {})
{
    auto const n = static_cast<uint8_t>(std::popcount(orig_mask));
    return UpwardTreeNode::make(UpwardTreeNode{
        .parent = parent,
        .old = std::move(old),
        .children = allocators::owning_span<ChildData>{n},
        .path = path,
        .opt_leaf_data = opt_leaf_data,
        .mask = orig_mask,
        .orig_mask = orig_mask,
        .branch = branch,
        .npending = n,
        .trie_section = trie_section,
        .prefix_index = static_cast<uint8_t>(prefix_index)});
}

static_assert(sizeof(UpwardTreeNode) == 80);
static_assert(alignof(UpwardTreeNode) == 8);

MONAD_MPT_NAMESPACE_END
