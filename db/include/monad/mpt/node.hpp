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

// ChildData is for temporarily holding a child's data in the update recursion.
// TODO for async: children data are part of the state when doing update
// asynchronously, should allocate an array of ChildData or an array of
// byte_string on heap instead of on current stack frame, which will be
// destructed when async
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

/* A note on generic trie

In Ethereum merkle patricia trie:
- Node is a extension if relpath len > 0, it only has one child, a branch node
- Node is a branch if mask > 0 && relpath len == 0, branch can have leaf value
- Node is a leaf node if it has no child

In generic trie, a node can have dual identity of ext and branch node, and
branch node can have vt (value) and be a leaf node at the same time. Branch node
with leaf data can have 1 child or more.
- A node with non-empty relpath is either an ext node or a leaf node
- A leaf node has is_leaf = true, however not necessarily leaf_len > 0
- A branch node with leaf can mean it's the end of an internal trie, making
itself also the root of the trie underneath, for example a leaf of an
account trie, where the account has an underlying storage trie. It can also
simply mean it's a branch node inside an internal trie, for example a branch
node with value in a receipt trie (var key length). Such branch node with leaf
will cache an intemediate hash inline.

Similar like a merkle patricia trie, each node's data is computed from its child
nodes data. Triedb is divided into different sections, indexed by unique
prefixes (i.e. sections for accounts, storages, receipts, etc.), node data is
defined differently in each section, and we leave the actual computation to the
`class Compute` to handle it.
We store node data to its parent's storage to avoid an extra read of child node
to retrieve child data.
*/
class Node
{
    struct _prevent_public_construction_tag
    {
    };

public:
    using data_off_t = uint16_t;

    /* 16-bit mask for children */
    uint16_t mask{0};
    /* is a leaf node, leaf_len is not necessarily positive */
    bool is_leaf;
    /* size (in byte) of user-passed leaf data */
    uint8_t leaf_len{0};
    /* size (in byte) of intermediate cache for branch hash */
    uint8_t hash_len{0};
    bool path_nibble_index_start{false};
    uint8_t path_nibble_index_end{0};
    char pad[1];
    unsigned char data[0];
    /* Data layout that exceeds node struct size is organized as below:
    * `n` is the number of children the node has and equals bitmask_count(mask)
    * `next` array: size-n array for children's mem pointers
    * `fnext` array: size-n array for children's on-disk offsets [TODO]
    * `data_offset` array: size-n array each stores a specific child data's
    starting offset
    * `path`: a few bytes for relative path, size depends on
    path_nibble_index_start, path_nibble_index_end
    * `leaf_data`: user-passed leaf data of leaf_len bytes
    * `hash_data`: intermediate hash cached for a implicit branch node, which
    exists in leaf nodes that have child (TODO: in the current version,
    extension node also has it, but will remove for this case).
    * `data_arr`: concatenated data bytes for all children
    */

    // TODO:
    // 1. get rid of data_off_arr, and store child data bytes as rlp encoded
    // bytes
    // 2. children data_arr can be stored out of line or we reuse nodes instead
    // of allocating new ones when node size remains the same, as most of the
    // time only one child out of multiple is updated, and other children's data
    // remains unchanged, storing inline requires copying them all, storing data
    // out of line allows us to transfer ownership of data array from old node
    // to new one, also help to keep allocated size as small as possible.

    using type_allocator = std::allocator<Node>;
#if !MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    // upper bound = (8 + (32 + 2 + 8 + 8) * 16 + 110 + 32 + 32)
    // assuming 8-byte mem pointers and on-disk offsets for now
    // 110: max leaf data bytes
    // 32: max relpath bytes
    // 32: max intermediate branch hash bytes stored inline
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
        // node size requested to allocate, n, not always equals a boost pool
        // size, here rounds n up => lower_bound + k * divisor */
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

    void set_params(
        uint16_t const mask_, bool const is_leaf_, uint8_t const leaf_len_,
        uint8_t const hash_len_)
    {
        mask = mask_;
        is_leaf = is_leaf_;
        leaf_len = leaf_len_;
        hash_len = hash_len_;
    }

    constexpr unsigned to_j(uint16_t i) const noexcept
    {
        // convert the enabled i'th bit in a 16-bit mask into its corresponding
        // index location - j
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
    constexpr unsigned child_data_len_j(unsigned const j)
    {
        return child_off_j(j + 1) - child_off_j(j);
    }

    constexpr unsigned child_data_len(unsigned const i)
    {
        return child_data_len_j(to_j(i));
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
        return (path_nibble_index_end + 1) / 2;
    }
    constexpr NibblesView path_nibble_view() const noexcept
    {
        return NibblesView{
            path_nibble_index_start, path_nibble_index_end, path_data()};
    }
    void set_path(NibblesView const relpath)
    {
        // TODO: a possible case isn't handled is that when si and ei are all
        // odd, should shift leaf one nibble, however this introduces more
        // memcpy. Might be worth doing in the serialization step.
        path_nibble_index_start = relpath.si;
        path_nibble_index_end = relpath.ei;
        if (relpath.size()) {
            std::memcpy(path_data(), relpath.data, relpath.size());
        }
    }
    constexpr bool has_relpath() const noexcept
    {
        return path_nibble_index_end > 0;
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
        if (data.size()) {
            std::memcpy(leaf_data(), data.data(), data.size());
        }
    }
    constexpr byte_string_view leaf_view() const noexcept
    {
        return {leaf_data(), leaf_len};
    }
    constexpr std::optional<byte_string_view> opt_leaf() const noexcept
    {
        if (is_leaf) {
            return leaf_view();
        }
        return std::nullopt;
    }

    //! hash
    constexpr bool has_hash() const noexcept
    {
        return hash_len;
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
            static_cast<size_t>(child_data_len_j(j))};
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
node_ptr create_leaf(byte_string_view const data, NibblesView const relpath);

//! create node: either branch/extension, with or without leaf
node_ptr create_node(
    Compute &comp, uint16_t const orig_mask, uint16_t const mask,
    std::span<ChildData> hashes, std::span<node_ptr> nexts,
    NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data = std::nullopt);

//! create a new node from a old node with possibly shorter relpath and an
//! optional new leaf data
node_ptr update_node_shorter_path(
    Node *old, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data = std::nullopt);

MONAD_MPT_NAMESPACE_END