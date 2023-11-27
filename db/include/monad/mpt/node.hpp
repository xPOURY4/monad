#pragma once

#include <monad/async/storage_pool.hpp>
#include <monad/core/byte_string.hpp>
#include <monad/core/math.hpp>
#include <monad/mem/allocators.hpp>
#include <monad/mpt/detail/unsigned_20.hpp>
#include <monad/mpt/util.hpp>

#include <cstdint>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute;
class NibblesView;

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
- A leaf node has has_value = true, however not necessarily value_len > 0
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
    struct prevent_public_construction_tag
    {
    };

public:
    using data_off_t = uint16_t;

    /* 16-bit mask for children */
    uint16_t mask{0};

    struct bitpacked_storage_t
    {
        /* does node have a value, value_len is not necessarily positive */
        bool has_value : 1 {false};
        bool path_nibble_index_start : 1 {false};
    } bitpacked{0};
    static_assert(sizeof(bitpacked) == 1);
    static_assert(
        std::endian::native == std::endian::little,
        "C bitfields stored to disk have the endian of their machine, big "
        "endian would need a bit swapping loader implementation");

    /* size (in byte) of user-passed leaf data */
    uint8_t value_len{0};
    /* size (in byte) of intermediate cache for branch hash */
    uint8_t data_len{0};
    uint8_t path_nibble_index_end{0};
    /* node on disk size */
    uint16_t disk_size{0}; // in bytes, max possible ~1000

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    unsigned char fnext_data[0];
#pragma GCC diagnostic pop
    /* Member funcs and data layout that exceeds node struct size is organized
    as below:
    * `number_of_children()` is the number of children the node has and equals
    std::popcount(mask)
    * `fnext` array: size-n array storing children's on-disk offsets
    * `data_offset` array: size-n array each stores a specific child data's
    starting offset
    * `path`: a few bytes for relative path, size depends on
    path_nibble_index_start, path_nibble_index_end
    * `value`: user-passed leaf data of value_len bytes
    * `data`: intermediate hash cached for a implicit branch node, which
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
    static constexpr size_t raw_bytes_allocator_allocation_divisor = 16;
    static constexpr size_t raw_bytes_allocator_allocation_lower_bound =
        round_up<size_t>(8, raw_bytes_allocator_allocation_divisor);
    static constexpr size_t raw_bytes_allocator_allocation_upper_bound =
        round_up<size_t>(
            (8 + (32 + 2 + 4 + 8 + 8) * 16 + 110 + 32 + 32),
            raw_bytes_allocator_allocation_divisor);

#if !MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    // upper bound = round_up(8 + (32 + 2 + 4 + 8 + 8) * 16 + 110 + 32 + 32)
    // assuming 8-byte mem pointers and on-disk offsets for now
    // 110: max leaf data bytes
    // 32: max relpath bytes
    // 32: max intermediate branch hash bytes stored inline
    using raw_bytes_allocator = allocators::array_of_boost_pools_allocator<
        raw_bytes_allocator_allocation_lower_bound,
        raw_bytes_allocator_allocation_upper_bound,
        raw_bytes_allocator_allocation_divisor>;
#else
    using raw_bytes_allocator = allocators::malloc_free_allocator<std::byte>;
#endif

    using allocator_pair_type = allocators::detail::type_raw_alloc_pair<
        type_allocator, raw_bytes_allocator>;
    static allocator_pair_type pool();

    static size_t get_deallocate_count(Node *);
    using unique_ptr_type = std::unique_ptr<
        Node, allocators::unique_ptr_aliasing_allocator_deleter<
                  type_allocator, raw_bytes_allocator, &Node::pool,
                  &Node::get_deallocate_count>>;

    Node(prevent_public_construction_tag);
    Node(Node const &) = delete;
    Node(Node &&) = default;
    ~Node();

    static unique_ptr_type make_node(unsigned size);

    void set_params(
        uint16_t mask, bool has_value, uint8_t value_len, uint8_t data_len);

    unsigned to_index(unsigned branch) const noexcept;

    unsigned number_of_children() const noexcept;

    //! fnext
    chunk_offset_t &fnext_index(unsigned index) noexcept;
    chunk_offset_t &fnext(unsigned branch) noexcept;

    //! min_block_no array
    unsigned char *child_min_count_data() noexcept;
    unsigned char const *child_min_count_data() const noexcept;
    detail::unsigned_20 &min_count_index(unsigned index) noexcept;
    detail::unsigned_20 &min_count(unsigned branch) noexcept;

    //! data_offset array
    unsigned char *child_off_data() noexcept;
    unsigned char const *child_off_data() const noexcept;
    data_off_t child_off_index(unsigned index) noexcept;

    unsigned child_data_len_index(unsigned index);
    unsigned child_data_len(unsigned branch);

    //! path
    unsigned char *path_data() noexcept;
    unsigned char const *path_data() const noexcept;
    unsigned path_nibbles_len() const noexcept;
    bool has_relpath() const noexcept;
    unsigned path_bytes() const noexcept;
    NibblesView path_nibble_view() const noexcept;

    //! value
    unsigned char *value_data() noexcept;
    unsigned char const *value_data() const noexcept;
    bool has_value() const noexcept;
    void set_value(byte_string_view value) noexcept;
    byte_string_view value() const noexcept;
    std::optional<byte_string_view> opt_value() const noexcept;

    //! data
    unsigned char *data_data() noexcept;
    unsigned char const *data_data() const noexcept;
    byte_string_view data() const noexcept;

    //! child data
    unsigned char *child_data() noexcept;
    byte_string_view child_data_view_index(unsigned index) noexcept;
    unsigned char *child_data_index(unsigned index) noexcept;
    unsigned char *child_data(unsigned branch) noexcept;
    byte_string_view child_data_view(unsigned branch) noexcept;
    void set_child_data_index(unsigned index, byte_string_view data) noexcept;

    //! next pointers
    unsigned char *next_data() noexcept;
    Node *next_index(unsigned const index) noexcept;
    Node *next(unsigned const branch) noexcept;
    void set_next_index(unsigned const index, Node *const node) noexcept;
    void set_next(unsigned const branch, Node *const node) noexcept;
    unique_ptr_type next_ptr_index(unsigned const index) noexcept;
    unique_ptr_type next_ptr(unsigned const branch) noexcept;

    //! node size in memory
    unsigned get_mem_size() noexcept;

    uint16_t get_disk_size() noexcept;
};

static_assert(sizeof(Node) == 8);
static_assert(alignof(Node) == 2);
using node_ptr = Node::unique_ptr_type;

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
    unsigned char data[32];
    detail::unsigned_20 min_count{uint32_t(-1)};
    uint8_t branch{INVALID_BRANCH};
    uint8_t len{0};
};
static_assert(sizeof(ChildData) == 56);
static_assert(alignof(ChildData) == 8);

detail::unsigned_20 calc_min_count(Node *, detail::unsigned_20 curr_count);

// create leaf node without children, data_len = 0
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
    NibblesView relpath, std::optional<byte_string_view> value = std::nullopt);

/* create a new node from a old node with possibly shorter relpath and an
optional new leaf data
Copy old with new relpath and new leaf, new relpath might be shortened */
Node *update_node_diff_path_leaf(
    Node *old, NibblesView relpath,
    std::optional<byte_string_view> value = std::nullopt);

Node *
create_node_nodata(uint16_t mask, NibblesView relpath, bool has_value = false);

void serialize_node_to_buffer(unsigned char *write_pos, Node *);

node_ptr deserialize_node_from_buffer(unsigned char const *read_pos);

Node *read_node_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool &, chunk_offset_t node_offset,
    unsigned bytes_to_read = 3U << DISK_PAGE_BITS);

MONAD_MPT_NAMESPACE_END
