#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>

MONAD_MPT_NAMESPACE_BEGIN

// In-memory find. Only call it for the block no section of the trie or where
// one is sure that part of trie is in memory
Node *find_in_mem_trie(
    Node *node, byte_string_view key, std::optional<unsigned> opt_node_pi)
{
    unsigned node_pi = opt_node_pi.has_value()
                           ? opt_node_pi.value()
                           : node->bitpacked.path_nibble_index_start;
    if (!node) {
        return nullptr;
    }
    unsigned pi = 0;
    unsigned const key_nibble_size = 2 * key.size();
    while (pi < key_nibble_size) {
        unsigned char nibble = get_nibble(key.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return nullptr;
            }
            // go to node's matched child
            node = node->next(nibble);
            MONAD_ASSERT(node); // nodes indexed by `key` should be in memory
            node_pi = node->bitpacked.path_nibble_index_start;
            ++pi;
            continue;
        }
        if (nibble != get_nibble(node->path_data(), node_pi)) {
            return nullptr;
        }
        // nibble is matched
        ++pi;
        ++node_pi;
    }
    if (node_pi != node->path_nibble_index_end) {
        // prefix key exists but no leaf ends at `key`
        return nullptr;
    }
    return node;
}

MONAD_MPT_NAMESPACE_END