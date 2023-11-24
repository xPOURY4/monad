#include <monad/async/config.hpp>

#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/util.hpp>

#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

node_ptr copy_node(
    UpdateAux &aux, node_ptr root, NibblesView const src,
    NibblesView const dest)
{
    MONAD_ASYNC_NAMESPACE::storage_pool *pool =
        aux.is_on_disk() ? &aux.io->storage_pool() : nullptr;
    auto [src_leaf, res] = find_blocking(pool, root.get(), src);
    MONAD_ASSERT(res == find_result::success);

    Node *parent = nullptr;
    Node *node = root.get();
    Node *new_node = nullptr;
    unsigned prefix_index = 0;
    unsigned node_prefix_index = root->bitpacked.path_nibble_index_start;
    unsigned char branch_i = INVALID_BRANCH;

    // Disconnect src_leaf's children
    // Insert `dest` to trie, the node created will need to have the same
    // children as node at `src`
    while (prefix_index < dest.nibble_size()) {
        unsigned char const nibble = dest.get(prefix_index);
        if (node->path_nibble_index_end == node_prefix_index) {
            if (node->mask & (1u << nibble) && node->next(nibble) != nullptr) {
                // go to node's matched child
                parent = node;
                branch_i = nibble;
                node = node->next(nibble);
                node_prefix_index = node->bitpacked.path_nibble_index_start;
                ++prefix_index;
                continue;
            }
            // add a branch = nibble to new_node
            new_node = [&]() -> Node * {
                MONAD_DEBUG_ASSERT(
                    prefix_index < std::numeric_limits<unsigned char>::max());
                Node *leaf = update_node_diff_path_leaf(
                    src_leaf,
                    dest.substr(static_cast<unsigned char>(prefix_index) + 1u),
                    src_leaf->value());
                // create a node, with no leaf data
                uint16_t const mask =
                    static_cast<uint16_t>(node->mask | (1u << nibble));
                Node *ret = create_node_nodata(mask, node->path_nibble_view());
                for (unsigned i = 0, j = 0, old_j = 0, bit = 1;
                     j < ret->number_of_children();
                     ++i, bit <<= 1) {
                    if (i == nibble) {
                        ret->set_next_j(j++, leaf);
                    }
                    else if (mask & bit) {
                        // assume child has no data for now
                        if (aux.is_on_disk()) {
                            ret->min_count_j(j) = node->min_count_j(old_j);
                            ret->fnext_j(j++) = node->fnext_j(old_j);
                        }
                        // also clear node's child mem ptr
                        aux.is_on_disk()
                            ? node->next_j_ptr(old_j++).reset()
                            : ret->set_next_j(
                                  j++, node->next_j_ptr(old_j++).release());
                    }
                }
                return ret;
            }();
            break;
        }
        auto const node_nibble =
            get_nibble(node->path_data(), node_prefix_index);
        if (nibble == node_nibble) {
            ++prefix_index;
            ++node_prefix_index;
            continue;
        }
        // mismatch: split node's path: turn node to a branch node with two
        // children
        new_node = [&]() -> Node * {
            MONAD_DEBUG_ASSERT(
                prefix_index < std::numeric_limits<unsigned char>::max());
            Node *dest_leaf = update_node_diff_path_leaf(
                src_leaf,
                dest.substr(static_cast<unsigned char>(prefix_index) + 1u),
                src_leaf->value());
            Node *node_latter_half = update_node_diff_path_leaf(
                node,
                NibblesView{
                    node_prefix_index + 1,
                    node->path_nibble_index_end,
                    node->path_data()},
                node->has_value()
                    ? std::optional<byte_string_view>{node->value()}
                    : std::nullopt);
            uint16_t const mask =
                static_cast<uint16_t>((1u << nibble) | (1u << node_nibble));
            Node *ret = create_node_nodata(
                mask,
                NibblesView{
                    node->bitpacked.path_nibble_index_start,
                    node_prefix_index,
                    node->path_data()});
            bool const leaf_first = nibble < node_nibble;
            ret->set_next_j(leaf_first ? 0 : 1, dest_leaf);
            ret->set_next_j(leaf_first ? 1 : 0, node_latter_half);
            if (aux.is_on_disk()) {
                // Request a write for the modified node, async_write_node()
                // serialize nodes to buffer but it's not guaranteed to land on
                // disk immediately because the buffer will only be flushed to
                // disk when full or close to full. To avoid reading from offset
                // that is still pending on write, we always cache
                // `node_latter_half` in memory here. TODO once we enable the
                // write back cache, we can unpin the node from memory no
                // problem.
                auto off =
                    async_write_node(aux, node_latter_half).offset_written_to;
                auto const pages =
                    num_pages(off.offset, node_latter_half->get_disk_size());
                MONAD_DEBUG_ASSERT(
                    pages <= std::numeric_limits<uint16_t>::max());
                off.spare = static_cast<uint16_t>(pages);
                ret->fnext_j(leaf_first ? 1 : 0) = off;
            }
            return ret;
        }();
        break;
    }
    if (prefix_index ==
        dest.nibble_size()) { // found an existing dest leaf in memory
        // node is the dest_leaf, need to recreate new_node to have the same
        // children as src_leaf, then deallocate the existing one
        assert(new_node == nullptr);
        assert(node != root.get());
        new_node = update_node_diff_path_leaf(
            src_leaf, node->path_nibble_view(), src_leaf->value());
        // clear parent's children other than new_node
        if (aux.is_on_disk()) {
            for (unsigned j = 0; j < parent->number_of_children(); ++j) {
                if (j != parent->to_j(branch_i)) {
                    parent->next_j_ptr(j).reset();
                };
            }
        }
    }
    // deallocate previous child at branch i
    if (parent) {
        parent->next_ptr(branch_i).reset(); // deallocate child (= node)
        parent->set_next(branch_i, new_node);
    }
    else {
        assert(node == root.get());
        root = node_ptr{new_node}; // deallocate root (= node)
    }
    return root;
}

MONAD_MPT_NAMESPACE_END
