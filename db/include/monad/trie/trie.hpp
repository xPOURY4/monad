#pragma once

#include <cstddef>

#include <monad/trie/config.hpp>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/merge.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

class TmpTrie;
struct tnode_t;

class MerkleTrie final
{
    merkle_node_t *root_;
    tnode_t *root_tnode_;

public:
    MerkleTrie()
        : root_(nullptr)
        , root_tnode_(nullptr){};

    ~MerkleTrie()
    {
        MONAD_TRIE_ASSERT(!root_tnode_ || !root_tnode_->npending);
    };

    void
    process_updates(TmpTrie *tmp_trie, merkle_node_t *prev_root, AsyncIO &io_)
    {
        root_tnode_ = get_new_tnode(NULL, 0, 0, NULL);
        root_ = do_merge(prev_root, tmp_trie->get_root(), 0, root_tnode_, io_);

        // after update, also need to poll until no submission left in uring
        io_.flush(root_);
    }

    [[gnu::always_inline]] void root_hash(unsigned char *const hash_data)
    {
        encode_branch(root_, hash_data);
    }

    [[gnu::always_inline]] constexpr merkle_node_t *get_root() { return root_; }
};

static_assert(sizeof(MerkleTrie) == 16);
static_assert(alignof(MerkleTrie) == 8);

MONAD_TRIE_NAMESPACE_END