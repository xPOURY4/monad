#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/mem/allocators.hpp>

#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/util.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>

MONAD_MPT_NAMESPACE_BEGIN

class Node;

// ChildData is used to temporarily hold children data in update recursion.
// TODO: children data are part of the state when doing update
// asynchronously, should allocate an array of ChildData or an array of
// byte_string on heap instead of in current stack frame, which will be
// destructed when async
#define INVALID_BRANCH 255

struct ChildData
{
    unsigned char data[32];
    uint8_t branch{INVALID_BRANCH};
    uint8_t len{0};
    char pad[6];
};
static_assert(sizeof(ChildData) == 40);
static_assert(alignof(ChildData) == 1);

inline void set_child_data(ChildData &dest, byte_string_view src)
{
    std::memcpy(dest.data, src.data(), src.size());
    dest.len = src.size();
}

/* In-memory trie node struct (TODO: on-disk)
Ethereum spec:
Node is extension if relpath len > 0 and no leaf data
Node is branch if mask > 0, relpath len == 0, branch can have leaf value
Node is a pure leaf node if leaf_len != 0, mask = 0
Generic Trie:
a node in generic trie can be an ext and a branch at the same time
*/
class Node
{
    struct _prevent_public_construction_tag
    {
    };

public:
    using data_off_t = uint16_t;
    // file_offset_t leaf_off;
    uint16_t mask{0};
    uint8_t leaf_len{0};
    uint8_t hash_len{0};
    bool path_si{false};
    uint8_t path_ei{0};
    char pad[2]; // TODO: remove padding
    unsigned char data[0];
    // layout:
    // next[n], (fnext[n]), data_off[n], path, leaf_data, hash_data,
    // data_arr[total_length]

    using type_allocator = std::allocator<Node>;
#if !MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    // upper bound = (8 + (32 + 2 + 8 + 8) * 16 + 110 + 32 + 32)
    // assumming 8-byte in-memory and on-disk offset for now
    // 110: max leaf data length
    // 32: max relpath length
    // 32: max branch hash length stored inline
    using raw_bytes_allocator = allocators::array_of_boost_pools_allocator<
        8, (8 + (32 + 2 + 8 + 8) * 16 + 110 + 32 + 32), 36, 8>;
#else
    using raw_bytes_allocator = allocators::malloc_free_allocator<std::byte>;
#endif

    using allocator_pair_type = allocators::detail::type_raw_alloc_pair<
        type_allocator, raw_bytes_allocator>;
    static allocator_pair_type pool()
    {
        static type_allocator a;
        static raw_bytes_allocator b;
        return {a, b};
    }
    static inline size_t get_allocated_count(unsigned n)
    {
        size_t res = ((n - raw_bytes_allocator::allocation_lower_bound) /
                          raw_bytes_allocator::allocation_divisor +
                      1) *
                         raw_bytes_allocator::allocation_divisor +
                     raw_bytes_allocator::allocation_lower_bound;
        MONAD_DEBUG_ASSERT(res >= n);
        return res;
    }
    static inline size_t get_deallocate_count(Node *p)
    {
        return get_allocated_count(p->node_mem_size());
    }
    using unique_ptr_type = std::unique_ptr<
        Node, allocators::unique_ptr_aliasing_allocator_deleter<
                  type_allocator, raw_bytes_allocator, &Node::pool,
                  &Node::get_deallocate_count>>;
    constexpr Node(_prevent_public_construction_tag) {}
    Node(const Node &) = delete;
    Node(Node &&) = default;
    inline ~Node();
    static inline unique_ptr_type make_node(unsigned size);

    void set_params(uint16_t mask_, uint8_t leaf_len_, uint8_t hash_len_)
    {
        mask = mask_;
        leaf_len = leaf_len_;
        hash_len = hash_len_;
    }

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

    unique_ptr_type next_j_ptr(unsigned const j) noexcept
    {
        Node *p = next_j(j);
        next_j(j) = nullptr;
        return unique_ptr_type{p};
    }

    unique_ptr_type next_ptr(unsigned const i) noexcept
    {
        return next_j_ptr(to_j(i));
    }

    void set_next_j(unsigned const j, unique_ptr_type ptr) noexcept
    {
        next_j(j) = ptr.release();
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
    void set_path(NibblesView const &relpath)
    {
        // TODO: a possible case isn't handled is that when si and ei are all
        // odd, should shift leaf one nibble
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
    constexpr bool has_hash() noexcept
    {
        return n() > 1 && (has_relpath() || leaf_len);
    }
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
using node_ptr = Node::unique_ptr_type;

inline Node::~Node()
{
    for (uint8_t j = 0; j < n(); ++j) {
        next_j_ptr(j).reset();
    }
}

inline Node::unique_ptr_type Node::make_node(unsigned storagebytes)
{
    return allocators::allocate_aliasing_unique<
        type_allocator,
        raw_bytes_allocator,
        &Node::pool,
        &Node::get_deallocate_count>(
        Node::get_allocated_count(storagebytes),
        _prevent_public_construction_tag{});
}

struct Compute;
//! create leaf node without children, hash_len = 0
node_ptr create_leaf(byte_string_view const data, NibblesView const &relpath);

//! create node: either branch/extension, with or without leaf
node_ptr create_node(
    Compute &comp, uint16_t const orig_mask, uint16_t const mask,
    std::span<ChildData> hashes, std::span<node_ptr> nexts,
    NibblesView const &relpath, byte_string_view const leaf_data = {});

//! create new leaf from old node with shorter relpath and new leaf data
node_ptr update_node_shorter_path(
    Node *old, NibblesView const &relpath,
    byte_string_view const leaf_data = {});

MONAD_MPT_NAMESPACE_END