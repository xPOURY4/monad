#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/util.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute;
class Node;

node_ptr upsert(Compute &comp, Node *const old, UpdateList &&updates);

inline Node *find(Node *node, byte_string_view key)
{
    unsigned pi = 0, node_pi = node->bitpacked.path_nibble_index_start;

    while (pi < 2 * key.size()) {
        unsigned char nibble = get_nibble(key.data(), pi);
        if (node->path_nibble_index_end == node_pi) {
            if (!(node->mask & (1u << nibble))) {
                return nullptr;
            }
            // go to next node's matching branch
            node = node->next(nibble);
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
    return node;
}

MONAD_MPT_NAMESPACE_END