#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>

MONAD_MPT_NAMESPACE_BEGIN

Node *read_node_blocking_(
    MONAD_ASYNC_NAMESPACE::storage_pool &pool, Node *parent,
    unsigned char branch)
{
    auto offset = parent->fnext(branch);
    // top 2 bits are for no_pages
    auto const num_pages_to_load_node = offset.spare;
    assert(num_pages_to_load_node <= 3);
    auto const bytes_to_read = num_pages_to_load_node << DISK_PAGE_BITS;
    return read_node_blocking(
        pool, offset, static_cast<unsigned int>(bytes_to_read));
}

find_result_type find_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node *node,
    NibblesView const key, std::optional<unsigned> opt_node_pi)
{
    if (!node) {
        return {nullptr, find_result::root_node_is_null_failure};
    }
    unsigned pi = 0, node_pi = opt_node_pi.has_value()
                                   ? opt_node_pi.value()
                                   : node->bitpacked.path_nibble_index_start;
    while (pi < key.nibble_size()) {
        unsigned char nibble = key.get(pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return {nullptr, find_result::branch_not_exist_failure};
            }
            // go to node's matched child
            if (!node->next(nibble)) { // read node if not yet in mem
                MONAD_ASSERT(pool != nullptr);
                node->set_next(
                    nibble, read_node_blocking_(*pool, node, nibble));
            }
            node = node->next(nibble);
            MONAD_ASSERT(node); // nodes indexed by `key` should be in memory
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_pi)) {
            return {nullptr, find_result::key_mismatch_failure};
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    if (node_pi != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return {nullptr, find_result::key_ends_ealier_than_node_failure};
    }
    return {node, find_result::success};
}

MONAD_MPT_NAMESPACE_END
