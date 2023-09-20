#include <monad/core/assert.h>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>

#include <optional>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

//! create leaf node without children, hash_len = 0
node_ptr create_leaf(byte_string_view const data, NibblesView const relpath)
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
    return node;
}

//! Note: there's a potential superfluous extension hash recomputation when node
//! coaleases upon erases, because we compute node hash when relpath is not yet
//! the final form. There's not yet a good way to avoid this unless we delay all
//! the compute() after all child branches finish creating nodes and return in
//! the recursion
inline node_ptr create_coalesced_node_with_prefix(
    ChildData &hash, node_ptr prev, NibblesView const prefix)
{
    // Note that prev may be a leaf
    Nibbles relpath = concat3(prefix, hash.branch, prev->path_nibble_view());
    // To get hash: no need to recompute because node and prev have the same
    // children, hash is either in prev->hash_data() if any or in `hash`
    unsigned hash_len = 0;
    unsigned char *hash_data = nullptr;
    if (prev->has_hash()) {
        hash_len = prev->hash_len;
        hash_data = prev->hash_data();
    }
    else if (prev->n()) {
        hash_len = hash.len;
        hash_data = hash.data;
    }
    // create node
    unsigned size = prev->node_mem_size() + relpath.size() -
                    prev->path_bytes() + (prev->hash_len ? 0 : hash_len);
    node_ptr node = Node::make_node(size);
    // copy node, nexts, data_off
    std::memcpy(
        (void *)node.get(),
        (void *)prev.get(),
        (uintptr_t)prev->path_data() - (uintptr_t)prev.get());
    node->hash_len = hash_len;
    node->is_leaf = prev->is_leaf;

    for (unsigned j = 0; j < prev->n(); ++j) {
        prev->next_j(j) = nullptr;
    }
    node->set_path(relpath);
    node->set_leaf(prev->leaf_view());
    if (hash_len) {
        std::memcpy(node->hash_data(), hash_data, hash_len);
    }
    std::memcpy(
        node->child_data(), prev->child_data(), prev->child_off_j(prev->n()));
    return node;
}

//! create node can either be a branch or extension or leaf with children
node_ptr create_node(
    Compute &comp, uint16_t const orig_mask, uint16_t const mask,
    std::span<ChildData> hashes, std::span<node_ptr> nexts,
    NibblesView const relpath, std::optional<byte_string_view> const leaf_data)
{
    // handle non child and single child cases
    unsigned const n = bitmask_count(mask);
    if (n == 0) {
        if (leaf_data.has_value()) {
            return create_leaf(leaf_data.value(), relpath);
        }
        return {};
    }
    else if (n == 1 && !leaf_data.has_value()) {
        unsigned j = bitmask_index(orig_mask, std::countr_zero(mask));
        return create_coalesced_node_with_prefix(
            hashes[j], std::move(nexts[j]), relpath);
    }
    MONAD_DEBUG_ASSERT(n > 1 || (n == 1 && leaf_data.has_value()));
    // any node with child will have hash data
    bool const is_leaf = leaf_data.has_value();
    uint8_t const leaf_len = is_leaf ? leaf_data.value().size() : 0,
                  hash_len =
                      ((relpath.size() || is_leaf)
                           ? comp.compute_len(hashes, nexts)
                           : 0);
    unsigned bytes = sizeof(Node) + leaf_len + hash_len + n * sizeof(Node *) +
                     relpath.size() + n * sizeof(Node::data_off_t);
    Node::data_off_t offsets[n];
    unsigned data_len = 0;
    for (unsigned i = 0, j = 0; i < hashes.size(); ++i) {
        if (nexts[i]) {
            MONAD_DEBUG_ASSERT(hashes[i].branch != INVALID_BRANCH);
            data_len += hashes[i].len;
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
    // set next and data
    for (unsigned i = 0, j = 0; i < hashes.size(); ++i) {
        if (nexts[i]) {
            node->set_next_j(j, std::move(nexts[i]));
            node->set_child_data_j(j++, {hashes[i].data, hashes[i].len});
        }
    }
    // compute branch() here can avoid duplicate compute for branch hash
    // once node is created, no field inside node should be changed
    if (node->hash_len) {
        // special case, node has one single branch
        comp.compute_branch(node->hash_data(), node.get());
    }
    return node;
}

//! Copy old with new relpath and new leaf, new relpath might be shorter than
//! previous. One corner case being that new relpath is empty, old's hash data
//! need to be removed when creating new node.
node_ptr update_node_shorter_path(
    Node *old, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data)
{
    MONAD_DEBUG_ASSERT(relpath.ei <= old->path_nibble_index_end);
    bool const is_leaf = leaf_data.has_value();
    unsigned hash_len = relpath.size() ? old->hash_len : 0,
             leaf_len = leaf_data.has_value() ? leaf_data.value().size() : 0;

    unsigned bytes = old->node_mem_size() + leaf_len - old->leaf_len +
                     relpath.size() - old->path_bytes() + hash_len -
                     old->hash_len;
    node_ptr node = Node::make_node(bytes);
    // copy Node, nexts and data_off array
    std::memcpy(
        (void *)node.get(),
        old,
        ((uintptr_t)old->path_data() - (uintptr_t)old));
    node->hash_len = hash_len;
    node->is_leaf = is_leaf;
    // order is enforced, must set path first
    node->set_path(relpath); // overwrite old path
    if (is_leaf) {
        node->set_leaf(leaf_data.value());
    }
    if (hash_len) {
        std::memcpy(node->hash_data(), old->hash_data(), hash_len);
    }
    if (old->n()) {
        std::memcpy(
            node->child_data(), old->child_data(), old->child_off_j(old->n()));
        // clear old nexts
        for (unsigned j = 0; j < old->n(); ++j) {
            old->next_j(j) = nullptr;
        }
    }
    MONAD_DEBUG_ASSERT(old->mask == node->mask);
    MONAD_DEBUG_ASSERT(node->path_nibble_view() == relpath);
    return node;
}

MONAD_MPT_NAMESPACE_END