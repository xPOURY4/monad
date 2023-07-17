#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/trie/config.hpp>
#include <monad/trie/index.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/request.hpp>
#include <monad/trie/tnode.hpp>

#if (__GNUC__ == 12 || __GNUC__ == 13) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds"
    #pragma GCC diagnostic ignored "-Wstringop-overread"
#endif

MONAD_TRIE_NAMESPACE_BEGIN

static const byte_string empty_trie_hash{
    {0x56, 0xe8, 0x1f, 0x17, 0x1b, 0xcc, 0x55, 0xa6, 0xff, 0x83, 0x45,
     0xe6, 0x92, 0xc0, 0xf8, 0x6e, 0x5b, 0x48, 0xe0, 0x1b, 0x99, 0x6c,
     0xad, 0xc0, 0x01, 0x62, 0x2f, 0xb5, 0xe3, 0x63, 0xb4, 0x21}};

void update_callback(void *user_data);
class MerkleTrie final
{
    merkle_node_t *root_;
    std::shared_ptr<AsyncIO> io_;
    std::shared_ptr<index_t> index_;
    const int cache_levels_;

public:
    MerkleTrie(
        merkle_node_t *root = nullptr, std::shared_ptr<AsyncIO> io = {},
        std::shared_ptr<index_t> index = {}, int cache_levels = 5)
        : root_(root)
        , io_(std::move(io))
        , index_(std::move(index))
        , cache_levels_(cache_levels)
    {
    }

    ~MerkleTrie()
    {
        if (root_) {
            free_trie(root_);
        }
    };

    ////////////////////////////////////////////////////////////////////
    // Update helper functions
    ////////////////////////////////////////////////////////////////////

    merkle_node_t *do_update(
        merkle_node_t *const prev_root, SubRequestInfo &nextlevel,
        tnode_t *curr_tnode, unsigned char const pi = 0);

    void update_trie(
        Request::unique_ptr_type updates, unsigned char pi,
        merkle_node_t *const new_parent, uint8_t const new_child_ni,
        tnode_t *parent_tnode);

    void build_new_trie(
        merkle_node_t *const parent, uint8_t const arr_idx,
        Request::unique_ptr_type updates);

    void upward_update_data(tnode_t *curr_tnode);

    ////////////////////////////////////////////////////////////////////
    // StateDB Interface
    ////////////////////////////////////////////////////////////////////

    void process_updates(monad::mpt::UpdateList &updates, uint64_t block_id = 0)
    {
        merkle_node_t *prev_root = root_ ? root_ : get_new_merkle_node(0, 0);
        MONAD_ASSERT(prev_root);

        Request::unique_ptr_type updateq = Request::make(std::move(updates));
        SubRequestInfo requests;
        updateq = updateq->split_into_subqueues(
            std::move(updateq), &requests, /*not root*/ false);

        tnode_t::unique_ptr_type root_tnode =
            get_new_tnode(nullptr, 0, 0, nullptr);
        root_ = do_update(prev_root, requests, root_tnode.get());

        file_offset_t root_off = 0;
        if (io_) {
            // after update, also need to poll until no submission left in uring
            // and write record to the indexing part in the beginning of file
            root_off = io_->flush(root_).offset_written_to;
            if (index_) {
                index_->write_record(block_id, root_off);
            }
        }
        // tear down previous trie version and free tnode
        MONAD_ASSERT(!root_tnode || !root_tnode->npending);
        free_trie(prev_root);
    }

    void root_hash(unsigned char *const dest)
    {
        unsigned nsubnodes_valid = std::popcount(root_->valid_mask);

        if (MONAD_UNLIKELY(!nsubnodes_valid)) {
            memcpy(dest, empty_trie_hash.data(), 32);
        }
        else if (MONAD_UNLIKELY(nsubnodes_valid == 1)) {
            uint8_t only_child = std::countr_zero(root_->valid_mask);
            auto *child =
                &root_->children[merkle_child_index(root_, only_child)];
            set_nibble(child->path, 0, only_child);
            unsigned char relpath[33];
            encode_two_piece(
                compact_encode(
                    relpath,
                    child->path,
                    0,
                    child->path_len(),
                    child->path_len() == 64),
                byte_string_view{child->data, child->data_len()},
                dest);
        }
        else {
            encode_branch(root_, dest);
        }
    }

    constexpr merkle_node_t *get_root()
    {
        return root_;
    }

    AsyncIO *get_io()
    {
        return io_.get();
    }

    void set_root(merkle_node_t *root)
    {
        root_ = root;
    }
};

static_assert(sizeof(MerkleTrie) == 48);
static_assert(alignof(MerkleTrie) == 8);

MONAD_TRIE_NAMESPACE_END

#if (__GNUC__ == 12 || __GNUC__ == 13) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif