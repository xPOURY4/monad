#include <monad/core/assert.h>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/traverse.hpp>

MONAD_MPT_NAMESPACE_BEGIN

void preorder_traverse(Node const &node, TraverseMachine &traverse)
{
    traverse.down(node);
    for (size_t i = 0; i < node.number_of_children(); ++i) {
        auto const *const next = node.next(i);
        MONAD_DEBUG_ASSERT(next); // only in-memory supported for now
        preorder_traverse(*next, traverse);
    }
    traverse.up(node);
}

MONAD_MPT_NAMESPACE_END
