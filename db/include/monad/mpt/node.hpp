#pragma once

#include <monad/async/storage_pool.hpp>
#include <monad/core/byte_string.hpp>
#include <monad/core/endian.hpp> // NOLINT
#include <monad/core/math.hpp>
#include <monad/mem/allocators.hpp>
#include <monad/mpt/detail/unsigned_20.hpp>
#include <monad/mpt/util.hpp>
#include <monad/rlp/encode.hpp>

#include <cstdint>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute;
class NibblesView;
class Node;
struct TrieStateMachine;

static constexpr size_t size_of_node = 8;
constexpr size_t calculate_node_size(
    size_t const number_of_children, size_t const total_child_data_size,
    size_t const value_size, size_t const path_size,
    size_t const data_size) noexcept
{
    MONAD_DEBUG_ASSERT(number_of_children || total_child_data_size == 0);
    return size_of_node +
           (sizeof(uint16_t) // child data offset
            + sizeof(detail::unsigned_20) // min count
            + sizeof(chunk_offset_t) + sizeof(Node *)) *
               number_of_children +
           total_child_data_size + value_size + path_size + data_size;
}

/* A note on generic trie

In Ethereum merkle patricia trie:
- Node is a extension if path len > 0, it only has one child, a branch node
- Node is a branch if mask > 0 && path len == 0, branch can have leaf value
- Node is a leaf node if it has no child

In generic trie, a node can have dual identity of ext and branch node, and
branch node can have vt (value) and be a leaf node at the same time. Branch node
with leaf data can have 1 child or more.
- A node with non-empty path is either an ext node or a leaf node
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
    static constexpr size_t max_value_size = rlp::list_length( // account rlp
        rlp::list_length(32) // balance
        + rlp::list_length(32) // code hash
        + rlp::list_length(32) // storage hash
        + rlp::list_length(8) // nonce
    );
    static constexpr size_t max_children = 16;
    static constexpr size_t max_size = calculate_node_size(
        max_children, max_children * 32, max_value_size, 32, 32);
    static constexpr size_t max_disk_size = max_size - (sizeof(Node *) * 16);
#if !MONAD_CORE_ALLOCATORS_DISABLE_BOOST_OBJECT_POOL_ALLOCATOR
    static constexpr size_t allocator_divisor = 16;
    using BytesAllocator = allocators::array_of_boost_pools_allocator<
        round_up<size_t>(size_of_node, allocator_divisor),
        round_up<size_t>(max_size, allocator_divisor), allocator_divisor>;
    static_assert(max_size == 1046);
    static_assert(max_disk_size == 918);
    static_assert(BytesAllocator::allocation_upper_bound == 1056);
#else
    using BytesAllocator = allocators::malloc_free_allocator<std::byte>;
#endif

    static allocators::detail::type_raw_alloc_pair<
        std::allocator<Node>, BytesAllocator>
    pool();
    static size_t get_deallocate_count(Node *);
    using UniquePtr = std::unique_ptr<
        Node, allocators::unique_ptr_aliasing_allocator_deleter<
                  std::allocator<Node>, BytesAllocator, &Node::pool,
                  &Node::get_deallocate_count>>;

public:
    /* 16-bit mask for children */
    uint16_t mask{0};

    struct bitpacked_storage_t
    {
        /* does node have a value, value_len is not necessarily positive */
        bool has_value : 1 {false};
        bool path_nibble_index_start : 1 {false};
    } bitpacked{0};
    static_assert(sizeof(bitpacked) == 1);

    /* size (in byte) of user-passed leaf data */
    uint8_t value_len{0};
    /* size (in byte) of intermediate cache for branch hash */
    uint8_t data_len{0};
    uint8_t path_nibble_index_end{0};
    /* node on disk size */
    uint16_t disk_size{0};

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

    Node(prevent_public_construction_tag);
    Node(Node const &) = delete;
    Node(Node &&) = default;
    ~Node();

    static UniquePtr make(size_t);

    void set_params(
        uint16_t mask, bool has_value, size_t value_len, size_t data_len);

    unsigned to_child_index(unsigned branch) const noexcept;

    unsigned number_of_children() const noexcept;

    //! fnext
    chunk_offset_t &fnext(unsigned index) noexcept;

    //! min_block_no array
    unsigned char *child_min_count_data() noexcept;
    unsigned char const *child_min_count_data() const noexcept;
    detail::unsigned_20 &min_count(unsigned index) noexcept;

    //! data_offset array
    unsigned char *child_off_data() noexcept;
    unsigned char const *child_off_data() const noexcept;
    uint16_t child_data_offset(unsigned index) noexcept;

    unsigned child_data_len(unsigned index);
    unsigned child_data_len();

    //! path
    unsigned char *path_data() noexcept;
    unsigned char const *path_data() const noexcept;
    unsigned path_nibbles_len() const noexcept;
    bool has_path() const noexcept;
    unsigned path_bytes() const noexcept;
    NibblesView path_nibble_view() const noexcept;
    unsigned path_start_nibble() const noexcept;

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
    byte_string_view child_data_view(unsigned index) noexcept;
    unsigned char *child_data(unsigned index) noexcept;
    void set_child_data(unsigned index, byte_string_view data) noexcept;

    //! next pointers
    unsigned char *next_data() noexcept;
    Node *next(unsigned index) noexcept;
    void set_next(unsigned index, Node *) noexcept;
    UniquePtr next_ptr(unsigned index) noexcept;

    //! node size in memory
    unsigned get_mem_size() noexcept;
    uint16_t get_disk_size() noexcept;
};

static_assert(std::is_standard_layout_v<Node>, "required by offsetof");
static_assert(sizeof(Node) == size_of_node);
static_assert(sizeof(Node) == 8);
static_assert(alignof(Node) == 2);

// ChildData is for temporarily holding a child's info, including child ptr,
// file offset and hash data, in the update recursion.
struct ChildData
{
    Node *ptr{nullptr};
    chunk_offset_t offset{INVALID_OFFSET};
    unsigned char data[32] = {0};
    detail::unsigned_20 min_count{uint32_t(-1)};
    uint8_t branch{INVALID_BRANCH};
    uint8_t len{0};
    uint8_t trie_section{uint8_t(-1)};

    bool is_valid() const;
    void erase();
    void set_branch_and_section(unsigned i, uint8_t sec);
    void set_node_and_compute_data(Node *node, TrieStateMachine &sm);
    void copy_old_child(Node *old, unsigned i);
};
static_assert(sizeof(ChildData) == 56);
static_assert(alignof(ChildData) == 8);

detail::unsigned_20 calc_min_count(Node *, detail::unsigned_20 curr_count);

Node::UniquePtr
make_node(Node &from, NibblesView path, std::optional<byte_string_view> value);

Node::UniquePtr make_node(
    uint16_t mask, std::span<ChildData>, NibblesView path,
    std::optional<byte_string_view> value, size_t data_size);

Node::UniquePtr make_node(
    uint16_t mask, std::span<ChildData>, NibblesView path,
    std::optional<byte_string_view> value, byte_string_view data);

// create leaf node without children, data_len = 0
Node *create_leaf(byte_string_view data, NibblesView path);

/* Note: there's a potential superfluous extension hash recomputation when node
coaleases upon erases, because we compute node hash when path is not yet
the final form. There's not yet a good way to avoid this unless we delay all
the compute() after all child branches finish creating nodes and return in
the recursion */
Node *create_coalesced_node_with_prefix(
    uint8_t branch, Node::UniquePtr prev, NibblesView prefix);

// create node: either branch/extension, with or without leaf
Node *create_node(
    Compute &, uint16_t mask, std::span<ChildData> children, NibblesView path,
    std::optional<byte_string_view> value = std::nullopt);

Node *
create_node_nodata(uint16_t mask, NibblesView path, bool has_value = false);

void serialize_node_to_buffer(unsigned char *write_pos, Node const &);

Node::UniquePtr deserialize_node_from_buffer(unsigned char const *read_pos);

Node *read_node_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool &, chunk_offset_t node_offset,
    unsigned bytes_to_read = 3U << DISK_PAGE_BITS);

MONAD_MPT_NAMESPACE_END
