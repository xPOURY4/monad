#include <monad/core/assert.h>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>

#include <optional>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

Node *create_leaf(byte_string_view const data, NibblesView const relpath)
{
    unsigned const bytes = sizeof(Node) + relpath.size() + data.size();
    node_ptr node = Node::make_node(bytes);
    // order is enforced, must set path first
    MONAD_DEBUG_ASSERT(node->path_data() == node->data);
    if (relpath.size()) {
        node->set_path(relpath);
    }
    node->set_params(0, true, data.size(), 0);
    node->set_leaf(data);
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

Node *create_coalesced_node_with_prefix(
    uint8_t const branch, node_ptr prev, NibblesView const prefix)
{
    // Note that prev may be a leaf
    Nibbles relpath = concat3(prefix, branch, prev->path_nibble_view());
    unsigned size = prev->get_mem_size() + relpath.size() - prev->path_bytes();
    node_ptr node = Node::make_node(size);
    // copy node, fnexts, data_off
    std::memcpy(
        (unsigned char *)node.get(),
        (unsigned char *)prev.get(),
        (uintptr_t)prev->path_data() - (uintptr_t)prev.get());

    node->set_path(relpath);
    node->set_leaf(prev->leaf_view());
    // hash and data arr
    std::memcpy(
        node->hash_data(),
        prev->hash_data(),
        node->hash_len + node->child_off_j(node->n()));
    // copy nexts
    if (node->n()) {
        memcpy(
            node->next_data(), prev->next_data(), node->n() * sizeof(Node *));
    }
    for (unsigned j = 0; j < prev->n(); ++j) {
        prev->set_next_j(j, nullptr);
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size >= prev->disk_size);
    return node.release();
}

Node *create_node(
    Compute &comp, uint16_t const mask, std::span<ChildData> children,
    NibblesView const relpath, std::optional<byte_string_view> const leaf_data)
{
    unsigned const n = bitmask_count(mask);
    // any node with child will have hash data
    bool const is_leaf = leaf_data.has_value();
    uint8_t const leaf_len = is_leaf ? leaf_data.value().size() : 0,
                  hash_len = is_leaf ? comp.compute_len(children) : 0;
    unsigned bytes = sizeof(Node) + leaf_len + hash_len +
                     n * (sizeof(Node *) + sizeof(Node::data_off_t) +
                          sizeof(file_offset_t)) +
                     relpath.size();
    Node::data_off_t offsets[n];
    unsigned data_len = 0;
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            data_len += child.len;
            offsets[j++] = data_len;
        }
    }
    bytes += data_len;
    node_ptr node = Node::make_node(bytes); // zero initialized
    node->set_params(mask, is_leaf, leaf_len, hash_len);
    std::memcpy(node->child_off_data(), offsets, n * sizeof(Node::data_off_t));
    // order is enforced, must set path first
    if (relpath.size()) {
        node->set_path(relpath);
    }
    if (is_leaf) {
        node->set_leaf(leaf_data.value());
    }
    // set fnext, next and data
    for (unsigned j = 0; auto &child : children) {
        if (child.branch != INVALID_BRANCH) {
            node->fnext_j(j) = child.offset;
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

Node *update_node_shorter_path(
    Node *old, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data)
{
    MONAD_DEBUG_ASSERT(relpath.ei <= old->path_nibble_index_end);
    bool const is_leaf = leaf_data.has_value();
    unsigned const leaf_len =
        leaf_data.has_value() ? leaf_data.value().size() : 0;

    unsigned bytes = old->get_mem_size() + leaf_len - old->leaf_len +
                     relpath.size() - old->path_bytes();
    node_ptr node = Node::make_node(bytes);
    // copy Node, fnexts and data_off array
    std::memcpy(
        (void *)node.get(),
        old,
        ((uintptr_t)old->path_data() - (uintptr_t)old));
    node->leaf_len = leaf_len;
    assert(is_leaf == node->is_leaf());
    // order is enforced, must set path first
    node->set_path(relpath); // overwrite old path
    if (is_leaf) {
        node->set_leaf(leaf_data.value());
    }
    // copy hash and child data arr
    std::memcpy(
        node->hash_data(),
        old->hash_data(),
        node->hash_len + old->child_off_j(old->n()));
    // copy next array
    if (old->n()) {
        std::memcpy(
            node->next_data(), old->next_data(), node->n() * sizeof(Node *));
    }
    // clear old nexts
    for (unsigned j = 0; j < old->n(); ++j) {
        old->set_next_j(j, nullptr);
    }
    node->disk_size = node->get_disk_size();
    assert(node->disk_size < 1024);
    return node.release();
}

MONAD_MPT_NAMESPACE_END