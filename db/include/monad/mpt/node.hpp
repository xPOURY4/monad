#pragma once

#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/async/storage_pool.hpp>

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/unaligned.hpp>
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

// ChildData is for temporarily holding a child's info, including child ptr,
// file offset and hash data, in the update recursion.
// TODO for async: children data are part of the state when doing update
// asynchronously, should allocate an array of ChildData or an array of
// byte_string on heap instead of on current stack frame, which will be
// destructed when async
struct ChildData
{
    Node *ptr{nullptr};
    chunk_offset_t offset{INVALID_OFFSET};
    uint8_t branch{INVALID_BRANCH};
    unsigned char data[32];
    uint8_t len{0};
};
static_assert(sizeof(ChildData) == 56);
static_assert(alignof(ChildData) == 8);

inline void set_child_data(ChildData &dest, byte_string_view src)
{
    std::memcpy(dest.data, src.data(), src.size());
    dest.len = static_cast<uint8_t>(src.size());
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
    struct prevent_public_construction_tag_
    {
    };

public:
    using data_off_t = uint16_t;

    /* 16-bit mask for children */
    uint16_t mask{0};

    struct bitpacked_storage_t
    {
        /* is a leaf node, leaf_len is not necessarily positive */
        bool is_leaf : 1 {false};
        bool path_nibble_index_start : 1 {false};
    } bitpacked{0};
    static_assert(sizeof(bitpacked) == 1);
    static_assert(
        std::endian::native == std::endian::little,
        "C bitfields stored to disk have the endian of their machine, big "
        "endian would need a bit swapping loader implementation");

    /* size (in byte) of user-passed leaf data */
    uint8_t leaf_len{0};
    /* size (in byte) of intermediate cache for branch hash */
    uint8_t hash_len{0};
    uint8_t path_nibble_index_end{0};
    /* node on disk size */
    uint16_t disk_size{0}; // in bytes, max possible ~1000
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    unsigned char data[0];
#pragma GCC diagnostic pop
    /* Member funcs and data layout that exceeds node struct size is organized
    as below:
    * `n()` is the number of children the node has and equals
    bitmask_count(mask)
    * `fnext` array: size-n array storing children's on-disk offsets
    * `data_offset` array: size-n array each stores a specific child data's
    starting offset
    * `path`: a few bytes for relative path, size depends on
    path_nibble_index_start, path_nibble_index_end
    * `leaf_data`: user-passed leaf data of leaf_len bytes
    * `hash_data`: intermediate hash cached for a implicit branch node, which
    exists in leaf nodes that have child.
    * `data_arr`: concatenated data bytes for all children
    * `next` array: size-n array storing children's mem pointers
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
    static constexpr size_t raw_bytes_allocator_allocation_lower_bound = 8;
    static constexpr size_t raw_bytes_allocator_allocation_upper_bound =
        (8 + (32 + 2 + 8 + 8) * 16 + 110 + 32 + 32);
    static constexpr size_t raw_bytes_allocator_allocation_divisor = 36;
    static constexpr size_t
        raw_bytes_allocator_allocation_less_than_lower_bound = 8;
#if !MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    // upper bound = (8 + (32 + 2 + 8 + 8) * 16 + 110 + 32 + 32)
    // assuming 8-byte mem pointers and on-disk offsets for now
    // 110: max leaf data bytes
    // 32: max relpath bytes
    // 32: max intermediate branch hash bytes stored inline
    using raw_bytes_allocator = allocators::array_of_boost_pools_allocator<
        raw_bytes_allocator_allocation_lower_bound,
        raw_bytes_allocator_allocation_upper_bound,
        raw_bytes_allocator_allocation_divisor,
        raw_bytes_allocator_allocation_less_than_lower_bound>;
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
        size_t res = ((n - raw_bytes_allocator_allocation_lower_bound) /
                          raw_bytes_allocator_allocation_divisor +
                      1) *
                         raw_bytes_allocator_allocation_divisor +
                     raw_bytes_allocator_allocation_lower_bound;
        MONAD_DEBUG_ASSERT(res >= n);
        return res;
    }
    static inline size_t get_deallocate_count(Node *p)
    {
        return get_allocated_count(p->get_mem_size());
    }
    using unique_ptr_type = std::unique_ptr<
        Node, allocators::unique_ptr_aliasing_allocator_deleter<
                  type_allocator, raw_bytes_allocator, &Node::pool,
                  &Node::get_deallocate_count>>;
    constexpr Node(prevent_public_construction_tag_) {}
    Node(Node const &) = delete;
    Node(Node &&) = default;
    inline ~Node();
    static inline unique_ptr_type make_node(unsigned size);

    void set_params(
        uint16_t const mask_, bool const is_leaf_, uint8_t const leaf_len_,
        uint8_t const hash_len_)
    {
        mask = mask_;
        bitpacked.is_leaf = is_leaf_;
        leaf_len = leaf_len_;
        hash_len = hash_len_;
    }

    constexpr unsigned to_j(unsigned const i) const noexcept
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

    //! fnext
    constexpr unsigned char *fnext_data() noexcept
    {
        return data;
    }

    chunk_offset_t &fnext_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j < n());
        return reinterpret_cast<chunk_offset_t *>(fnext_data())[j];
    }

    chunk_offset_t &fnext(unsigned const i) noexcept
    {
        MONAD_DEBUG_ASSERT(i < 16);
        return fnext_j(to_j(i));
    }

    //! data_offset array
    constexpr unsigned char *child_off_data() noexcept
    {
        return data + n() * sizeof(file_offset_t);
    }
    constexpr unsigned char const *child_off_data() const noexcept
    {
        return data + n() * sizeof(file_offset_t);
    }
    data_off_t child_off_j(unsigned const j) noexcept
    {
        MONAD_DEBUG_ASSERT(j <= n());
        if (j == 0) {
            return 0;
        }
        else {
            data_off_t res;
            memcpy(
                &res,
                child_off_data() + (j - 1) * sizeof(data_off_t),
                sizeof(data_off_t));
            return res;
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
    constexpr unsigned path_nibbles_len() const noexcept
    {
        return path_nibble_index_end - bitpacked.path_nibble_index_start;
    }

    constexpr bool has_relpath() const noexcept
    {
        return path_nibbles_len() > 0;
    }

    constexpr unsigned path_bytes() const noexcept
    {
        return (path_nibble_index_end + 1) / 2;
    }
    constexpr NibblesView path_nibble_view() const noexcept
    {
        return NibblesView{
            bitpacked.path_nibble_index_start,
            path_nibble_index_end,
            path_data()};
    }
    void set_path(NibblesView const relpath)
    {
        // TODO: a possible case isn't handled is that when si and ei are all
        // odd, should shift leaf one nibble, however this introduces more
        // memcpy. Might be worth doing in the serialization step.
        bitpacked.path_nibble_index_start = relpath.si;
        path_nibble_index_end = relpath.ei;
        if (relpath.size()) {
            std::memcpy(path_data(), relpath.data, relpath.size());
        }
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
    constexpr bool is_leaf() const noexcept
    {
        return bitpacked.is_leaf;
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
        if (is_leaf()) {
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

    //! next pointers
    constexpr unsigned char *next_data() noexcept
    {
        return child_data() + child_off_j(n());
    }

    constexpr Node *next_j(unsigned const j) noexcept
    {
        return unaligned_load<Node *>(next_data() + j * sizeof(Node *));
    }

    constexpr Node *next(unsigned const i) noexcept
    {
        return next_j(to_j(i));
    }

    void set_next_j(unsigned const j, Node *const node) noexcept
    {
        node ? memcpy(next_data() + j * sizeof(Node *), &node, sizeof(Node *))
             : memset(next_data() + j * sizeof(Node *), 0, sizeof(Node *));
    }
    void set_next(unsigned const i, Node *const node) noexcept
    {
        set_next_j(to_j(i), node);
    }

    unique_ptr_type next_j_ptr(unsigned const j) noexcept
    {
        Node *p = next_j(j);
        set_next_j(j, nullptr);
        return unique_ptr_type{p};
    }

    unique_ptr_type next_ptr(unsigned const i) noexcept
    {
        return next_j_ptr(to_j(i));
    }

    //! node size in memory
    constexpr unsigned get_mem_size() noexcept
    {
        auto const *const end = next_data() + sizeof(Node *) * n();
        MONAD_DEBUG_ASSERT(end >= (unsigned char *)this);
        return static_cast<unsigned>(end - (unsigned char *)this);
    }

    constexpr uint16_t get_disk_size() noexcept
    {
        MONAD_DEBUG_ASSERT(next_data() >= (unsigned char *)this);
        return static_cast<uint16_t>(next_data() - (unsigned char *)this);
    }
};

static_assert(sizeof(Node) == 8);
static_assert(alignof(Node) == 2);
using node_ptr = Node::unique_ptr_type;

inline Node::~Node()
{
    for (uint8_t j = 0; j < n(); ++j) {
        node_ptr{next_j(j)};
        set_next_j(j, nullptr);
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
        prevent_public_construction_tag_{});
}

struct Compute;
// create leaf node without children, hash_len = 0
Node *create_leaf(byte_string_view data, NibblesView relpath);

/* Note: there's a potential superfluous extension hash recomputation when node
coaleases upon erases, because we compute node hash when relpath is not yet
the final form. There's not yet a good way to avoid this unless we delay all
the compute() after all child branches finish creating nodes and return in
the recursion */
Node *create_coalesced_node_with_prefix(
    uint8_t branch, node_ptr prev, NibblesView prefix);

// create node: either branch/extension, with or without leaf
Node *create_node(
    Compute &, uint16_t mask, std::span<ChildData> children,
    NibblesView relpath,
    std::optional<byte_string_view> leaf_data = std::nullopt);

/* create a new node from a old node with possibly shorter relpath and an
optional new leaf data
Copy old with new relpath and new leaf, new relpath might be shortened */
Node *update_node_diff_path_leaf(
    Node *old, NibblesView relpath,
    std::optional<byte_string_view> leaf_data = std::nullopt);

inline Node *create_node_nodata(
    uint16_t const mask, NibblesView const relpath, bool const is_leaf = false)
{
    auto const n = bitmask_count(mask);
    auto const bytes =
        sizeof(Node) + relpath.size() +
        n * (sizeof(Node *) + sizeof(file_offset_t) + sizeof(Node::data_off_t));

    node_ptr node = Node::make_node(static_cast<unsigned int>(bytes));
    memset((void *)node.get(), 0, bytes);

    node->set_params(mask, is_leaf, /*leaf_len*/ 0, /*hash_len*/ 0);
    if (relpath.size()) {
        node->set_path(relpath);
    }
    node->disk_size = node->get_disk_size();
    return node.release();
}

inline void
serialize_node_to_buffer(unsigned char *const write_pos, Node *const node)
{
    memcpy(write_pos, node, node->disk_size);
    return;
}

inline node_ptr deserialize_node_from_buffer(unsigned char const *read_pos)
{
    uint16_t const mask = unaligned_load<uint16_t>(read_pos);
    unsigned const n = bitmask_count(mask);
    uint16_t const disk_size = unaligned_load<uint16_t>(read_pos + 6),
                   alloc_size =
                       static_cast<uint16_t>(disk_size + n * sizeof(Node *));
    node_ptr node = Node::make_node(alloc_size);
    memcpy((unsigned char *)node.get(), read_pos, disk_size);
    memset(node->next_data(), 0, n * sizeof(Node *));
    return node;
}

inline Node *read_node_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool &pool, chunk_offset_t node_offset,
    unsigned bytes_to_read = 3U << DISK_PAGE_BITS)
{
    file_offset_t rd_offset =
        round_down_align<DISK_PAGE_BITS>(node_offset.offset);
    uint16_t buffer_off = uint16_t(node_offset.offset - rd_offset);
    auto *buffer =
        (unsigned char *)aligned_alloc(DISK_PAGE_SIZE, bytes_to_read);
    auto unbuffer = make_scope_exit([buffer]() noexcept { ::free(buffer); });

    auto chunk = pool.activate_chunk(pool.seq, node_offset.id);
    auto fd = chunk->read_fd();
    ssize_t bytes_read = pread(
        fd.first,
        buffer,
        bytes_to_read,
        static_cast<off_t>(fd.second + rd_offset));
    if (bytes_read < 0) {
        fprintf(
            stderr,
            "FATAL: pread(%u, %llu) failed with '%s'\n",
            bytes_to_read,
            rd_offset,
            strerror(errno));
    }
    return deserialize_node_from_buffer(buffer + buffer_off).release();
}
MONAD_MPT_NAMESPACE_END
