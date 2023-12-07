#include <monad/core/assert.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/traverse.hpp>

MONAD_MPT_NAMESPACE_BEGIN

namespace
{
    void preorder_traverse_impl(
        unsigned char const branch, Node const &node, TraverseMachine &traverse)
    {
        traverse.down(branch, node);
        for (unsigned char i = 0; i < 16; ++i) {
            if (node.mask & (1u << i)) {
                auto const *const next = node.next(node.to_child_index(i));
                MONAD_DEBUG_ASSERT(next); // only in-memory supported for now
                preorder_traverse_impl(i, *next, traverse);
            }
        }
        traverse.up(branch, node);
    }
}

void preorder_traverse(Node const &node, TraverseMachine &traverse)
{
    preorder_traverse_impl(INVALID_BRANCH, node, traverse);
}

MONAD_MPT_NAMESPACE_END
