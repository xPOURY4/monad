#include <monad/mpt/node.hpp>

#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/unaligned.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/util.hpp>

#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

MONAD_MPT_NAMESPACE_BEGIN

Node::allocator_pair_type Node::pool()
{
    static Node::type_allocator a;
    static Node::raw_bytes_allocator b;
    return {a, b};
}

size_t Node::get_deallocate_count(Node *p)
{
    return p->get_mem_size();
}

Node::Node(prevent_public_construction_tag) {}

Node::~Node()
{
    for (uint8_t index = 0; index < number_of_children(); ++index) {
        node_ptr{next_index(index)};
        set_next_index(index, nullptr);
    }
}

Node::unique_ptr_type Node::make_node(unsigned storagebytes)
{
    return allocators::allocate_aliasing_unique<
        type_allocator,
        raw_bytes_allocator,
        &Node::pool,
        &Node::get_deallocate_count>(
        storagebytes, prevent_public_construction_tag{});
}

void Node::set_params(
    uint16_t const mask, bool const has_value, uint8_t const value_len,
    uint8_t const data_len)
{
    this->mask = mask;
    this->bitpacked.has_value = has_value;
    this->value_len = value_len;
    this->data_len = data_len;
}

unsigned Node::to_index(unsigned const branch) const noexcept
{
    // convert the enabled i'th bit in a 16-bit mask into its corresponding
    // index location - index
    MONAD_DEBUG_ASSERT(mask & (1u << branch));
    return bitmask_index(mask, branch);
}

unsigned Node::number_of_children() const noexcept
{
    return static_cast<unsigned>(std::popcount(mask));
}

chunk_offset_t &Node::fnext_index(unsigned const index) noexcept
{
    MONAD_DEBUG_ASSERT(index < number_of_children());
    return reinterpret_cast<chunk_offset_t *>(fnext_data)[index];
}

chunk_offset_t &Node::fnext(unsigned const branch) noexcept
{
    MONAD_DEBUG_ASSERT(branch < 16);
    return fnext_index(to_index(branch));
}

unsigned char *Node::child_min_count_data() noexcept
{
    return fnext_data + number_of_children() * sizeof(file_offset_t);
}

unsigned char const *Node::child_min_count_data() const noexcept
{
    return fnext_data + number_of_children() * sizeof(file_offset_t);
}

detail::unsigned_20 &Node::min_count_index(unsigned const index) noexcept
{
    return reinterpret_cast<detail::unsigned_20 *>(
        child_min_count_data())[index];
}

detail::unsigned_20 &Node::min_count(unsigned const branch) noexcept
{
    return min_count_index(to_index(branch));
}

unsigned char *Node::child_off_data() noexcept
{
    return child_min_count_data() + number_of_children() * sizeof(uint32_t);
}

unsigned char const *Node::child_off_data() const noexcept
{
    return child_min_count_data() + number_of_children() * sizeof(uint32_t);
}

Node::data_off_t Node::child_off_index(unsigned const index) noexcept
{
    MONAD_DEBUG_ASSERT(index <= number_of_children());
    if (index == 0) {
        return 0;
    }
    else {
        data_off_t res;
        memcpy(
            &res,
            child_off_data() + (index - 1) * sizeof(data_off_t),
            sizeof(data_off_t));
        return res;
    }
}

unsigned Node::child_data_len_index(unsigned const index)
{
    return child_off_index(index + 1) - child_off_index(index);
}

unsigned Node::child_data_len(unsigned const branch)
{
    return child_data_len_index(to_index(branch));
}

unsigned char *Node::path_data() noexcept
{
    return child_off_data() + number_of_children() * sizeof(data_off_t);
}

unsigned char const *Node::path_data() const noexcept
{
    return child_off_data() + number_of_children() * sizeof(data_off_t);
}

unsigned Node::path_nibbles_len() const noexcept
{
    return path_nibble_index_end - bitpacked.path_nibble_index_start;
}

bool Node::has_relpath() const noexcept
{
    return path_nibbles_len() > 0;
}

unsigned Node::path_bytes() const noexcept
{
    return (path_nibble_index_end + 1) / 2;
}

NibblesView Node::path_nibble_view() const noexcept
{
    return NibblesView{
        bitpacked.path_nibble_index_start, path_nibble_index_end, path_data()};
}

unsigned char *Node::value_data() noexcept
{
    return path_data() + path_bytes();
}

unsigned char const *Node::value_data() const noexcept
{
    return path_data() + path_bytes();
}

bool Node::has_value() const noexcept
{
    return bitpacked.has_value;
}

void Node::set_value(byte_string_view value) noexcept
{
    MONAD_DEBUG_ASSERT(value_len == value.size());
    if (value.size()) {
        std::memcpy(value_data(), value.data(), value.size());
    }
}

byte_string_view Node::value() const noexcept
{
    MONAD_DEBUG_ASSERT(has_value());
    return {value_data(), value_len};
}

std::optional<byte_string_view> Node::opt_value() const noexcept
{
    if (has_value()) {
        return value();
    }
    return std::nullopt;
}

unsigned char *Node::data_data() noexcept
{
    return value_data() + value_len;
}

unsigned char const *Node::data_data() const noexcept
{
    return value_data() + value_len;
}

byte_string_view Node::data() const noexcept
{
    return {data_data(), data_len};
}

unsigned char *Node::child_data() noexcept
{
    return data_data() + data_len;
}

byte_string_view Node::child_data_view_index(unsigned const index) noexcept
{
    MONAD_DEBUG_ASSERT(index < number_of_children());
    return byte_string_view{
        child_data() + child_off_index(index),
        static_cast<size_t>(child_data_len_index(index))};
}

unsigned char *Node::child_data_index(unsigned const index) noexcept
{
    MONAD_DEBUG_ASSERT(index < number_of_children());
    return child_data() + child_off_index(index);
}

unsigned char *Node::child_data(unsigned const branch) noexcept
{
    return child_data_index(to_index(branch));
}

byte_string_view Node::child_data_view(unsigned const branch) noexcept
{
    return child_data_view_index(to_index(branch));
}

void Node::set_child_data_index(
    unsigned const index, byte_string_view data) noexcept
{
    // called after data_off array is calculated
    std::memcpy(child_data_index(index), data.data(), data.size());
}

unsigned char *Node::next_data() noexcept
{
    return child_data() + child_off_index(number_of_children());
}

Node *Node::next_index(unsigned const index) noexcept
{
    return unaligned_load<Node *>(next_data() + index * sizeof(Node *));
}

Node *Node::next(unsigned const branch) noexcept
{
    return next_index(to_index(branch));
}

void Node::set_next_index(unsigned const index, Node *const node) noexcept
{
    node ? memcpy(next_data() + index * sizeof(Node *), &node, sizeof(Node *))
         : memset(next_data() + index * sizeof(Node *), 0, sizeof(Node *));
}
void Node::set_next(unsigned const branch, Node *const node) noexcept
{
    set_next_index(to_index(branch), node);
}

Node::unique_ptr_type Node::next_ptr_index(unsigned const index) noexcept
{
    Node *p = next_index(index);
    set_next_index(index, nullptr);
    return unique_ptr_type{p};
}

Node::unique_ptr_type Node::next_ptr(unsigned const branch) noexcept
{
    return next_ptr_index(to_index(branch));
}

unsigned Node::get_mem_size() noexcept
{
    auto const *const end = next_data() + sizeof(Node *) * number_of_children();
    MONAD_DEBUG_ASSERT(end >= (unsigned char *)this);
    return static_cast<unsigned>(end - (unsigned char *)this);
}

uint16_t Node::get_disk_size() noexcept
{
    MONAD_DEBUG_ASSERT(next_data() >= (unsigned char *)this);
    return static_cast<uint16_t>(next_data() - (unsigned char *)this);
}

detail::unsigned_20
calc_min_count(Node *const node, detail::unsigned_20 const curr_count)
{
    if (!node->mask) {
        return curr_count;
    }
    detail::unsigned_20 ret{uint32_t(-1)};
    for (unsigned index = 0; index < node->number_of_children(); ++index) {
        ret = std::min(ret, node->min_count_index(index));
    }
    MONAD_ASSERT(ret != detail::unsigned_20(uint32_t(-1)));
    return ret;
}

Node *create_leaf(byte_string_view const data, NibblesView const relpath)
{
    auto const bytes = sizeof(Node) + relpath.data_size() + data.size();
    MONAD_DEBUG_ASSERT(bytes <= std::numeric_limits<unsigned int>::max());
    node_ptr node = Node::make_node(static_cast<unsigned int>(bytes));
    // order is enforced, must set path first
    MONAD_DEBUG_ASSERT(node->path_data() == node->fnext_data);
    if (relpath.data_size()) {
        serialize_to_node(relpath, *node);
    }
    MONAD_DEBUG_ASSERT(data.size() <= std::numeric_limits<uint8_t>::max());
    node->set_params(0, true, static_cast<uint8_t>(data.size()), 0);
    node->set_value(data);
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

Node *create_coalesced_node_with_prefix(
    uint8_t const branch, node_ptr prev, NibblesView const prefix)
{
    // Note that prev may be a leaf
    Nibbles const relpath = concat3(prefix, branch, prev->path_nibble_view());
    unsigned const size =
        prev->get_mem_size() + relpath.data_size() - prev->path_bytes();
    node_ptr node = Node::make_node(size);
    // copy node, fnexts, min_count, data_off
    std::memcpy(
        (unsigned char *)node.get(),
        (unsigned char *)prev.get(),
        (uintptr_t)prev->path_data() - (uintptr_t)prev.get());

    serialize_to_node(NibblesView{relpath}, *node);
    if (prev->has_value()) {
        node->set_value(prev->value());
    }
    // hash and data arr
    std::memcpy(
        node->data_data(),
        prev->data_data(),
        node->data_len + node->child_off_index(node->number_of_children()));
    // copy nexts
    if (node->number_of_children()) {
        memcpy(
            node->next_data(),
            prev->next_data(),
            node->number_of_children() * sizeof(Node *));
    }
    for (unsigned j = 0; j < prev->number_of_children(); ++j) {
        prev->set_next_index(j, nullptr);
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size >= prev->disk_size);
    return node.release();
}

// all children's offset are set before creating parent
Node *create_node(
    Compute &comp, uint16_t const mask, std::span<ChildData> children,
    NibblesView const relpath, std::optional<byte_string_view> const value)
{
    auto const number_of_children = static_cast<unsigned>(std::popcount(mask));
    // any node with child will have hash data
    bool const has_value = value.has_value();
    uint8_t const value_len =
        has_value ? static_cast<uint8_t>(value.value().size()) : 0;
    uint8_t const data_len =
        has_value ? static_cast<uint8_t>(comp.compute_len(children, mask)) : 0;
    auto bytes =
        sizeof(Node) + value_len + data_len +
        number_of_children * (sizeof(Node *) + sizeof(Node::data_off_t) +
                              sizeof(uint32_t) + sizeof(file_offset_t)) +
        relpath.data_size();
    std::vector<Node::data_off_t> offsets(number_of_children);
    unsigned child_len = 0;
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            child_len += child.len;
            MONAD_DEBUG_ASSERT(
                child_len <= std::numeric_limits<Node::data_off_t>::max());
            offsets[j++] = static_cast<Node::data_off_t>(child_len);
        }
    }
    bytes += child_len;
    node_ptr node = Node::make_node(static_cast<unsigned int>(
        bytes)); // zero initialized in Node but not tail
    node->set_params(mask, has_value, value_len, data_len);
    std::memcpy(
        node->child_off_data(),
        offsets.data(),
        offsets.size() * sizeof(Node::data_off_t));
    // order is enforced, must set path first
    if (relpath.data_size()) {
        serialize_to_node(relpath, *node);
    }
    if (has_value) {
        node->set_value(value.value());
    }
    // set fnext, next and data
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            node->fnext_index(j) = child.offset;
            node->min_count_index(j) = child.min_count;
            node->set_next_index(j, child.ptr);
            node->set_child_data_index(j++, {child.data, child.len});
        }
    }
    if (node->data_len) {
        comp.compute_branch(node->data_data(), node.get());
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

Node *update_node_diff_path_leaf(
    Node *old, NibblesView const relpath,
    std::optional<byte_string_view> const value)
{
    bool const has_value = value.has_value();
    auto const value_len = value.has_value() ? value.value().size() : 0;
    MONAD_ASSERT(value_len < 255); // or uint8_t will overflow

    auto const bytes = old->get_mem_size() + value_len - old->value_len +
                       relpath.data_size() - old->path_bytes();
    MONAD_DEBUG_ASSERT(bytes <= std::numeric_limits<unsigned>::max());
    node_ptr node = Node::make_node(static_cast<unsigned>(bytes));
    // copy Node, fnexts and data_off array
    std::memcpy( // NOLINT
        (void *)node.get(),
        old,
        ((uintptr_t)old->path_data() - (uintptr_t)old));
    node->value_len = static_cast<uint8_t>(value_len);
    node->bitpacked.has_value = has_value;
    // order is enforced, must set path first
    serialize_to_node(relpath, *node); // overwrite old path
    if (has_value) {
        node->set_value(value.value());
    }
    // copy hash and child data arr
    std::memcpy(
        node->data_data(),
        old->data_data(),
        node->data_len + old->child_off_index(old->number_of_children()));
    // copy next array
    if (old->number_of_children()) {
        std::memcpy(
            node->next_data(),
            old->next_data(),
            node->number_of_children() * sizeof(Node *));
    }
    // clear old nexts
    for (unsigned j = 0; j < old->number_of_children(); ++j) {
        old->set_next_index(j, nullptr);
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

void serialize_to_node(NibblesView const nibbles, Node &node)
{
    // TODO: optimization opportunity when si and ei are all
    // odd, should shift leaf one nibble, however this introduces more
    // memcpy. Might be worth doing in the serialization step.
    node.bitpacked.path_nibble_index_start = nibbles.begin_nibble_;
    node.path_nibble_index_end = nibbles.end_nibble_;
    if (nibbles.data_size()) {
        std::memcpy(node.path_data(), nibbles.data_, nibbles.data_size());
    }
}

Node *create_node_nodata(
    uint16_t const mask, NibblesView const relpath, bool const has_value)
{
    auto const bytes = sizeof(Node) + relpath.data_size() +
                       static_cast<unsigned>(std::popcount(mask)) *
                           (sizeof(Node *) + sizeof(file_offset_t) +
                            sizeof(uint32_t) + sizeof(Node::data_off_t));

    node_ptr node = Node::make_node(static_cast<unsigned int>(bytes));
    memset((void *)node.get(), 0, bytes);

    node->set_params(mask, has_value, /*value_len*/ 0, /*data_len*/ 0);
    if (relpath.data_size()) {
        serialize_to_node(relpath, *node);
    }
    node->disk_size = node->get_disk_size();
    return node.release();
}

void serialize_node_to_buffer(unsigned char *const write_pos, Node *const node)
{
    MONAD_ASSERT(node->disk_size > 0 && node->disk_size <= MAX_DISK_NODE_SIZE);
    memcpy(write_pos, node, node->disk_size);
    return;
}

node_ptr deserialize_node_from_buffer(unsigned char const *read_pos)
{
    uint16_t const mask = unaligned_load<uint16_t>(read_pos);
    auto const number_of_children = static_cast<unsigned>(std::popcount(mask));
    uint16_t const disk_size = unaligned_load<uint16_t>(read_pos + 6),
                   alloc_size = static_cast<uint16_t>(
                       disk_size + number_of_children * sizeof(Node *));
    MONAD_ASSERT(disk_size > 0 && disk_size <= MAX_DISK_NODE_SIZE);
    node_ptr node = Node::make_node(alloc_size);
    memcpy((unsigned char *)node.get(), read_pos, disk_size);
    memset(node->next_data(), 0, number_of_children * sizeof(Node *));
    return node;
}

Node *read_node_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool &pool, chunk_offset_t node_offset,
    unsigned bytes_to_read)
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
