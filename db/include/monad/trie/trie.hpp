#pragma once

#include <cstddef>

#include <monad/trie/config.hpp>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/merge.hpp>
#include <time.h>

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

    void process_updates(
        uint64_t vid, TmpTrie *tmp_trie, merkle_node_t *prev_root, AsyncIO &io,
        Index &index)
    {
        struct timespec ts_before, ts_after;
        double tm_flush;

        root_tnode_ = get_new_tnode(nullptr, 0, 0, nullptr);
        root_ = do_merge(prev_root, tmp_trie->get_root(), 0, root_tnode_, io);

        // after update, also need to poll until no submission left in uring
        // and write record to the indexing part in the beginning of file
        clock_gettime(CLOCK_MONOTONIC, &ts_before);
        int64_t root_off = io.flush(root_);
        clock_gettime(CLOCK_MONOTONIC, &ts_after);
        tm_flush = ((double)ts_after.tv_sec + (double)ts_after.tv_nsec / 1e9) -
                   ((double)ts_before.tv_sec + (double)ts_before.tv_nsec / 1e9);
        fprintf(stdout, "flush time %f\n", tm_flush);
        printf(
            "vid %lu, root_off %ld, index ptr %lu\n",
            vid,
            root_off,
            (unsigned long)&index);
        // index.write_record(vid, root_off);
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