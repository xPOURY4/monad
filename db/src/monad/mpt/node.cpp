#include <monad/mpt/node.hpp>

#include <monad/core/assert.h>
#include <monad/mpt/compute.hpp>
#include <monad/core/byte_string.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/util.hpp>

#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <vector>

MONAD_MPT_NAMESPACE_BEGIN

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
    node->set_leaf(data);
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
    node->set_leaf(prev->leaf_view());
    // hash and data arr
    std::memcpy(
        node->hash_data(),
        prev->hash_data(),
        node->hash_len + node->child_off_j(node->number_of_children()));
    // copy nexts
    if (node->number_of_children()) {
        memcpy(
            node->next_data(),
            prev->next_data(),
            node->number_of_children() * sizeof(Node *));
    }
    for (unsigned j = 0; j < prev->number_of_children(); ++j) {
        prev->set_next_j(j, nullptr);
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size >= prev->disk_size);
    return node.release();
}

// all children's offset are set before creating parent
Node *create_node(
    Compute &comp, uint16_t const mask, std::span<ChildData> children,
    NibblesView const relpath, std::optional<byte_string_view> const leaf_data)
{
    auto const number_of_children = static_cast<unsigned>(std::popcount(mask));
    // any node with child will have hash data
    bool const is_leaf = leaf_data.has_value();
    uint8_t const leaf_len =
                      is_leaf ? static_cast<uint8_t>(leaf_data.value().size())
                              : 0,
                  hash_len =
                      is_leaf ? static_cast<uint8_t>(comp.compute_len(children))
                              : 0;
    auto bytes =
        sizeof(Node) + leaf_len + hash_len +
        number_of_children * (sizeof(Node *) + sizeof(Node::data_off_t) +
                              sizeof(uint32_t) + sizeof(file_offset_t)) +
        relpath.data_size();
    std::vector<Node::data_off_t> offsets(number_of_children);
    unsigned data_len = 0;
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            data_len += child.len;
            MONAD_DEBUG_ASSERT(
                data_len <= std::numeric_limits<Node::data_off_t>::max());
            offsets[j++] = static_cast<Node::data_off_t>(data_len);
        }
    }
    bytes += data_len;
    node_ptr node = Node::make_node(static_cast<unsigned int>(
        bytes)); // zero initialized in Node but not tail
    node->set_params(mask, is_leaf, leaf_len, hash_len);
    std::memcpy(
        node->child_off_data(),
        offsets.data(),
        offsets.size() * sizeof(Node::data_off_t));
    // order is enforced, must set path first
    if (relpath.data_size()) {
        serialize_to_node(relpath, *node);
    }
    if (is_leaf) {
        node->set_leaf(leaf_data.value());
    }
    // set fnext, next and data
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            node->fnext_j(j) = child.offset;
            node->min_count_j(j) = child.min_count;
            node->set_next_j(j, child.ptr);
            node->set_child_data_j(j++, {child.data, child.len});
        }
    }
    if (node->hash_len) {
        comp.compute_branch(node->hash_data(), node.get());
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

Node *update_node_diff_path_leaf(
    Node *old, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data)
{
    bool const is_leaf = leaf_data.has_value();
    auto const leaf_len = leaf_data.has_value() ? leaf_data.value().size() : 0;
    MONAD_ASSERT(leaf_len < 255); // or uint8_t will overflow

    auto const bytes = old->get_mem_size() + leaf_len - old->leaf_len +
                       relpath.data_size() - old->path_bytes();
    MONAD_DEBUG_ASSERT(bytes <= std::numeric_limits<unsigned>::max());
    node_ptr node = Node::make_node(static_cast<unsigned>(bytes));
    // copy Node, fnexts and data_off array
    std::memcpy(
        (void *)node.get(),
        old,
        ((uintptr_t)old->path_data() - (uintptr_t)old));
    node->leaf_len = static_cast<uint8_t>(leaf_len);
    node->bitpacked.is_leaf = is_leaf;
    // assert(is_leaf == node->is_leaf());
    // order is enforced, must set path first
    serialize_to_node(relpath, *node); // overwrite old path
    if (is_leaf) {
        node->set_leaf(leaf_data.value());
    }
    // copy hash and child data arr
    std::memcpy(
        node->hash_data(),
        old->hash_data(),
        node->hash_len + old->child_off_j(old->number_of_children()));
    // copy next array
    if (old->number_of_children()) {
        std::memcpy(
            node->next_data(),
            old->next_data(),
            node->number_of_children() * sizeof(Node *));
    }
    // clear old nexts
    for (unsigned j = 0; j < old->number_of_children(); ++j) {
        old->set_next_j(j, nullptr);
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

MONAD_MPT_NAMESPACE_END
