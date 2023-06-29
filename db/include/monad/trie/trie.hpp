#pragma once

#include <cstddef>

#include <monad/trie/config.hpp>

#include <monad/trie/index.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/request.hpp>
#include <monad/trie/tnode.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void update_callback(void *user_data, AsyncIO &io);

merkle_node_t *do_update(
    merkle_node_t *const prev_root, SubRequestInfo &nextlevel,
    tnode_t *curr_tnode, AsyncIO &io, unsigned char const pi = 0);

void update_trie(
    Request *const updates, unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode, AsyncIO &io);

void build_new_trie(
    merkle_node_t *const parent, uint8_t const arr_idx, Request *updates,
    AsyncIO &io);

class MerkleTrie final
{
    merkle_node_t *root_;
    tnode_t::unique_ptr_type root_tnode_;

public:
    constexpr MerkleTrie()
        : root_(nullptr)
    {
    }

    ~MerkleTrie()
    {
        MONAD_ASSERT(!root_tnode_ || !root_tnode_->npending);
    };

    void process_updates(
        uint64_t vid, monad::mpt::UpdateList &updates, merkle_node_t *prev_root,
        AsyncIO &io, Index &index)
    {
        Request *updateq = Request::pool.construct(updates);
        SubRequestInfo requests;
        updateq->split_into_subqueues(&requests, /*not root*/ false);

        root_tnode_ = get_new_tnode(nullptr, 0, 0, nullptr);
        root_ = do_update(prev_root, requests, root_tnode_.get(), io);

        // after update, also need to poll until no submission left in uring
        // and write record to the indexing part in the beginning of file
        int64_t root_off = io.flush(root_);
        index.write_record(vid, root_off);
    }

    void root_hash(unsigned char *const hash_data)
    {
        encode_branch(root_, hash_data);
    }

    constexpr merkle_node_t *get_root()
    {
        return root_;
    }
};

static_assert(sizeof(MerkleTrie) == 16);
static_assert(alignof(MerkleTrie) == 8);

MONAD_TRIE_NAMESPACE_END