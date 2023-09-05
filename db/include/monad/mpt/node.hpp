#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>

#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/util.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Node;

// ChildData is used to temporarily hold children data in update recursion.
// TODO: children data are part of the state when doing update
// asynchronously, should allocate an array of ChildData or an array of
// byte_string on heap instead of in current stack frame, which will be
// destructed when async
struct ChildData
{
    unsigned char data[32];
    uint8_t len{0};
    char pad[7];
};
static_assert(sizeof(ChildData) == 40);
static_assert(alignof(ChildData) == 1);

/* In-memory trie node struct (TODO: on-disk)
Ethereum spec:
Node is extension if relpath len > 0 and no leaf data
Node is branch if mask > 0, relpath len == 0, branch can have leaf value
Node is a pure leaf node if leaf_len != 0, mask = 0
Generic Trie:
a node in generic trie can be an ext and a branch at the same time
*/
struct Node
{
    using data_off_t = uint16_t;
    // file_offset_t leaf_off;
    uint16_t mask{0};
    uint8_t leaf_len{0};
    uint8_t hash_len{0};
    bool path_si{false};
    uint8_t path_ei{0};
    char pad[2];
    unsigned char data[0];
    // layout:
    // next[n], (fnext[n]), data_off[n], path, leaf_data, hash_data,
    // data_arr[total_length]

    constexpr unsigned to_j(uint16_t i) const noexcept
    {
        MONAD_DEBUG_ASSERT(mask & 1u << i);
        return bitmask_index(mask, i);
    }

    constexpr unsigned n() const noexcept
    {
        return bitmask_count(mask);
    }

    //! next ptrs
    constexpr unsigned char *next_data() noexcept
    {
        return data;
    }

    Node *&next_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j < n());
        return reinterpret_cast<Node **>(next_data())[j];
    }

    Node *&next(unsigned const i) noexcept
    {
        MONAD_DEBUG_ASSERT(i < 16);
        return next_j(to_j(i));
    }

    //! data_offset array
    constexpr unsigned char *child_off_data() noexcept
    {
        return data + n() * sizeof(Node *);
    }
    constexpr unsigned char const *child_off_data() const noexcept
    {
        return data + n() * sizeof(Node *);
    }
    data_off_t child_off_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j <= n());
        if (j == 0) {
            return 0;
        }
        else {
            return reinterpret_cast<data_off_t *>(child_off_data())[j - 1];
        }
    }

    //! path
    constexpr unsigned char *path_data() noexcept
    {
        return child_off_data() + n() * sizeof(data_off_t);
    }
    constexpr unsigned char const *path_data() const noexcept
    {
        return child_off_data() + n() * sizeof(data_off_t);
    }
    constexpr unsigned path_bytes() const noexcept
    {
        return (path_ei + 1) / 2;
    }
    constexpr NibblesView path_nibble_view() const noexcept
    {
        return NibblesView{path_si, path_ei, path_data()};
    }
    void set_path(NibblesView relpath)
    {
        // MONAD_DEBUG_ASSERT(relpath.size());
        // MONAD_DEBUG_ASSERT((uint8_t)relpath.si != relpath.ei);
        path_si = relpath.si;
        path_ei = relpath.ei;
        if (relpath.size()) {
            std::memcpy(path_data(), relpath.data, relpath.size());
        }
    }
    constexpr bool has_relpath() const noexcept
    {
        return path_ei > 0;
    }

    //! leaf
    constexpr unsigned char *leaf_data() noexcept
    {
        return path_data() + path_bytes();
    }
    constexpr unsigned char const *leaf_data() const noexcept
    {
        return path_data() + path_bytes();
    }
    void set_leaf(byte_string_view data) noexcept
    {
        MONAD_DEBUG_ASSERT(leaf_len == data.size());
        std::memcpy(leaf_data(), data.data(), data.size());
    }
    constexpr byte_string_view leaf_view() const noexcept
    {
        return {leaf_data(), leaf_len};
    }

    //! hash
    constexpr unsigned char *hash_data() noexcept
    {
        return leaf_data() + leaf_len;
    }
    constexpr unsigned char const *hash_data() const noexcept
    {
        return leaf_data() + leaf_len;
    }
    constexpr byte_string_view hash_view() const noexcept
    {
        return {hash_data(), hash_len};
    }

    //! child data
    constexpr unsigned char *child_data() noexcept
    {
        return hash_data() + hash_len;
    }
    byte_string_view child_data_view_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j < n());
        return byte_string_view{
            child_data() + child_off_j(j),
            static_cast<size_t>(child_off_j(j + 1) - child_off_j(j))};
    }
    constexpr unsigned char *child_data_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j < n());
        return child_data() + child_off_j(j);
    }
    constexpr unsigned char *child_data(unsigned const i) noexcept
    {
        return child_data_j(to_j(i));
    }
    constexpr byte_string_view child_data_view(unsigned const i) noexcept
    {
        return child_data_view_j(to_j(i));
    }
    void set_child_data_j(unsigned const j, byte_string_view data) noexcept
    {
        // called after data_off array is calculated
        std::memcpy(child_data_j(j), data.data(), data.size());
    }

    //! node size in memory
    constexpr unsigned node_mem_size() noexcept
    {
        return (child_data() - (unsigned char *)this) + child_off_j(n());
    }
};

static_assert(sizeof(Node) == 8);
static_assert(alignof(Node) == 2);

struct Compute;

//! create leaf node without children, hash_len = 0
Node *create_leaf(byte_string_view data, NibblesView &relpath);

//! create node: either branch/extension, with or without leaf
Node *create_node(
    Compute &comp, uint16_t const mask, std::span<ChildData> children,
    std::span<Node *> nexts, NibblesView &relpath,
    byte_string_view leaf_data = {});

//! create new leaf from old node with updated relpath and leaf data
Node *create_node_from_prev(
    Node *old, NibblesView &relpath, byte_string_view leaf_data = {});

inline void free_trie(Node *node)
{
    if (!node) {
        return;
    }
    for (unsigned j = 0; j < bitmask_count(node->mask); ++j) {
        free_trie(node->next_j(j));
    }
    free(node);
}

MONAD_MPT_NAMESPACE_END