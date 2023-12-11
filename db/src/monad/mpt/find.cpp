#include <monad/async/config.hpp>

#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/nibble.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/util.hpp>

#include <cassert>
#include <optional>

MONAD_MPT_NAMESPACE_BEGIN

Node *read_node_blocking_(
    MONAD_ASYNC_NAMESPACE::storage_pool &pool, Node *parent,
    unsigned char branch)
{
    auto offset = parent->fnext(parent->to_child_index(branch));
    // top 2 bits are for no_pages
    auto const num_pages_to_load_node = offset.spare;
    assert(num_pages_to_load_node <= 3);
    auto const bytes_to_read = num_pages_to_load_node << DISK_PAGE_BITS;
    return read_node_blocking(
        pool, offset, static_cast<unsigned int>(bytes_to_read));
}

find_result_type find_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node *node,
    NibblesView const key, std::optional<unsigned> opt_node_prefix_index)
{
    if (!node) {
        return {nullptr, find_result::root_node_is_null_failure};
    }
    unsigned prefix_index = 0;
    unsigned node_prefix_index = opt_node_prefix_index.has_value()
                                     ? opt_node_prefix_index.value()
                                     : node->bitpacked.path_nibble_index_start;
    while (prefix_index < key.nibble_size()) {
        unsigned char const nibble = key.get(prefix_index);
        if (node->path_nibble_index_end == node_prefix_index) {
            if (!(node->mask & (1u << nibble))) {
                return {nullptr, find_result::branch_not_exist_failure};
            }
            // go to node's matched child
            if (!node->next(node->to_child_index(nibble))) {
                // read node if not yet in mem
                MONAD_ASSERT(pool != nullptr);
                node->set_next(
                    node->to_child_index(nibble),
                    read_node_blocking_(*pool, node, nibble));
            }
            node = node->next(node->to_child_index(nibble));
            MONAD_ASSERT(node); // nodes indexed by `key` should be in memory
            node_prefix_index = node->bitpacked.path_nibble_index_start;
            ++prefix_index;
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_prefix_index)) {
            return {nullptr, find_result::key_mismatch_failure};
        }
        // nibble is matched
        ++prefix_index;
        ++node_prefix_index;
    }
    if (node_prefix_index != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return {nullptr, find_result::key_ends_ealier_than_node_failure};
    }
    return {node, find_result::success};
}

Nibbles
find_min_key_blocking(MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node &root)
{
    Nibbles path;
    Node *node = &root;
    while (!node->has_value()) {
        MONAD_DEBUG_ASSERT(node->number_of_children() > 0);
        path = concat(
            NibblesView{path},
            node->path_nibble_view(),
            static_cast<unsigned char>(std::countr_zero(node->mask)));
        // go to next node
        if (!node->next(0)) {
            MONAD_ASSERT(pool);
            node->set_next(0, read_node_blocking(*pool, node->fnext(0)));
        }
        node = node->next(0);
    }
    return concat(NibblesView{path}, node->path_nibble_view());
}

Nibbles
find_max_key_blocking(MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node &root)
{
    Nibbles path;
    Node *node = &root;
    while (!node->has_value()) {
        MONAD_DEBUG_ASSERT(node->number_of_children() > 0);
        path = concat(
            NibblesView{path},
            node->path_nibble_view(),
            static_cast<unsigned char>(15 - std::countl_zero(node->mask)));
        // go to next node
        if (!node->next(node->number_of_children() - 1)) {
            MONAD_ASSERT(pool);
            node->set_next(
                node->number_of_children() - 1,
                read_node_blocking(
                    *pool, node->fnext(node->number_of_children() - 1)));
        }
        node = node->next(node->number_of_children() - 1);
    }
    return concat(NibblesView{path}, node->path_nibble_view());
}

MONAD_MPT_NAMESPACE_END
