#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>

#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute
{
    virtual ~Compute(){};
    //! compute length of hash from a span of child data
    virtual unsigned compute_len(std::span<ChildData> hashes) = 0;
    //! compute data of a trie rooted at node, fill in node's hash_data if
    //! hash_len > 0, put trie data to first argument and return data length
    //! 3rd parameter is the branch nibble of node, it's present only when node
    //! is the single child of its parent, which is a leaf node
    virtual unsigned
    compute(unsigned char *buffer, Node *node, unsigned nibble = -1) = 0;
};

struct EmptyCompute final : Compute
{
    virtual unsigned compute_len(std::span<ChildData>) override
    {
        return 0;
    }

    virtual unsigned compute(unsigned char *, Node *, unsigned = -1) override
    {
        return 0;
    }
};

// TODO: implement merkle
struct MerkleCompute final : Compute
{
    virtual unsigned compute_len(std::span<ChildData>) override
    {
        return 0;
    }

    virtual unsigned
    compute(unsigned char *, Node *node, unsigned = -1) override
    {
        if (node->leaf_len) {
            // compute leaf
            return 0;
        }
        MONAD_DEBUG_ASSERT(node->n() > 1);
        if (node->has_relpath()) {
            // compute branch node hash to node->hash_data()
            // compute extension node hash from rlp([relpath, branch_hash])
        }
        else {
            // compute branch node hash to dest
        }
        return 0;
    }
};

MONAD_MPT_NAMESPACE_END