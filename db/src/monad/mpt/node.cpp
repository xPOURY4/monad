#include <monad/core/assert.h>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>

#include <span>

MONAD_MPT_NAMESPACE_BEGIN

//! create leaf node without children, hash_len = 0
node_ptr create_leaf(byte_string_view data, NibblesView &relpath)
{
    unsigned const bytes = sizeof(Node) + relpath.size() + data.size();
    node_ptr node = Node::make_node(bytes);
    // order is enforced, must set path first
    MONAD_DEBUG_ASSERT(node->path_data() == node->data);
    if (relpath.size()) {
        node->set_path(relpath);
    }
    node->leaf_len = data.size();
    node->set_leaf(data);
    return node;
}

//! create node can either be a branch or extension or leaf with children
node_ptr create_node(
    Compute &comp, uint16_t const mask, std::span<ChildData> hashes,
    std::span<node_ptr> nexts, NibblesView &relpath, byte_string_view leaf_data)
{
    unsigned const n = bitmask_count(mask);
    MONAD_DEBUG_ASSERT(n);
    uint8_t leaf_len = leaf_data.size(), hash_len = 0;
    if (n > 1 && (leaf_len || relpath.size())) {
        hash_len = comp.compute_len(hashes);
    }
    unsigned bytes = sizeof(Node) + leaf_len + hash_len + n * sizeof(Node *) +
                     relpath.size() + n * sizeof(Node::data_off_t);
    Node::data_off_t offsets[n];
    unsigned data_len = 0;
    for (unsigned j = 0; j < hashes.size(); ++j) {
        data_len += hashes[j].len;
        offsets[j] = data_len;
    }
    bytes += data_len;

    node_ptr node = Node::make_node(bytes);
    node->set_params(mask, leaf_len, hash_len);
    std::memcpy(node->child_off_data(), offsets, n * sizeof(Node::data_off_t));
    // order is enforced, must set path first
    if (relpath.size()) {
        node->set_path(relpath);
    }
    if (leaf_len) {
        node->set_leaf(leaf_data);
    }
    // set data
    for (unsigned j = 0; j < hashes.size(); ++j) {
        node->set_child_data_j(j, {hashes[j].data, hashes[j].len});
        node->set_next_j(j, std::move(nexts[j]));
    }

    return node;
}

node_ptr create_node_from_prev(
    Node *old, NibblesView &relpath, byte_string_view leaf_data)
{
    unsigned bytes = old->node_mem_size() + (leaf_data.size() - old->leaf_len) +
                     (relpath.size() - old->path_bytes());
    node_ptr node = Node::make_node(bytes);
    // copy Node, nexts and data_off array
    std::memcpy(
        (void *)node.get(),
        old,
        ((uintptr_t)old->path_data() - (uintptr_t)old));
    // order is enforced, must set path first
    node->set_path(relpath);
    if (leaf_data.size()) {
        node->set_leaf(leaf_data);
    }
    if (old->n()) {
        // hash and child data only exists when node has any children
        std::memcpy(
            node->hash_data(),
            old->hash_data(),
            old->child_data() + old->child_off_j(old->n()) - old->hash_data());
        // clear old nexts
        for (unsigned j = 0; j < old->n(); ++j) {
            old->next_j(j) = nullptr;
        }
    }
    return node;
}

MONAD_MPT_NAMESPACE_END