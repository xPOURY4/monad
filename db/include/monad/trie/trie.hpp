#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>
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

static const byte_string empty_trie_hash = [] {
    using namespace ::monad::literals;
    return 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex;
}();

void update_callback(void *user_data);

class MerkleTrie final
{
    merkle_node_ptr root_;
    std::shared_ptr<AsyncIO> io_;
    std::shared_ptr<index_t> index_;
    const int cache_levels_;

public:
    MerkleTrie(
        merkle_node_ptr root = {}, std::shared_ptr<AsyncIO> io = {},
        std::shared_ptr<index_t> index = {}, int cache_levels = 5)
        : root_(std::move(root))
        , io_(std::move(io))
        , index_(std::move(index))
        , cache_levels_(cache_levels)
    {
    }

    MerkleTrie(
        file_offset_t const root_off, std::shared_ptr<AsyncIO> io,
        std::shared_ptr<index_t> index = {}, int cache_levels = 5)
        : root_(
              root_off == INVALID_OFFSET ? get_new_merkle_node(0, 0)
                                         : read_node(io->get_rd_fd(), root_off))
        , io_(std::move(io))
        , index_(std::move(index))
        , cache_levels_(cache_levels)
    {
    }

    ////////////////////////////////////////////////////////////////////
    // Update helper functions
    ////////////////////////////////////////////////////////////////////

    merkle_node_ptr do_update(
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
        merkle_node_ptr prev_root =
            root_ ? std::move(root_) : get_new_merkle_node(0, 0);

        Request::unique_ptr_type updateq = Request::make(std::move(updates));
        SubRequestInfo requests;
        updateq = updateq->split_into_subqueues(
            std::move(updateq), &requests, /*not root*/ false);

        tnode_t::unique_ptr_type root_tnode =
            get_new_tnode(nullptr, 0, 0, nullptr);
        root_ = do_update(prev_root.get(), requests, root_tnode.get());

        if (io_) {
            // after update, also need to poll until no submission left in uring
            // and write record to the indexing part in the beginning of file
            file_offset_t root_off = io_->flush(root_.get()).offset_written_to;
            if (index_) {
                index_->write_record(block_id, root_off);
            }
        }
        // tear down previous version trie and free tnode
        MONAD_ASSERT(!root_tnode || !root_tnode->npending);
    }

    void flush_last_buffer()
    {
        io_->flush_last_buffer();
    }

    void root_hash(unsigned char *const dest)
    {
        unsigned nsubnodes_valid = std::popcount(root_->valid_mask);

        if (MONAD_UNLIKELY(!nsubnodes_valid)) {
            memcpy(
                dest,
                empty_trie_hash.data(),
                sizeof(merkle_child_info_t::path));
        }
        else if (MONAD_UNLIKELY(nsubnodes_valid == 1)) {
            uint8_t only_child = std::countr_zero(root_->valid_mask);
            auto *child =
                &root_->children()[merkle_child_index(root_.get(), only_child)];
            set_nibble(child->path, 0, only_child);
            unsigned char relpath[sizeof(merkle_child_info_t::path) + 1];
            encode_two_piece(
                compact_encode(
                    relpath,
                    child->path,
                    0,
                    child->path_len(),
                    child->path_len() == 64),
                byte_string_view{child->data.get(), child->data_len()},
                dest);
        }
        else {
            encode_branch(root_.get(), dest);
        }
    }

    ////////////////////////////////////////////////////////////////////
    // Accessor Implementation
    ////////////////////////////////////////////////////////////////////

private:
    // implement the blocking read
    std::optional<byte_string_view>
    read_helper(byte_string_view const &key, merkle_node_t const *const root)
    {
        MONAD_ASSERT(root->path_len == 0);

        // find the leaf with key as prefix
        unsigned pi = 0;
        merkle_node_t const *node = root;

        // root's branches
        unsigned char nibble = get_nibble(key.data(), pi++);
        if (!(node->valid_mask & 1u << nibble)) {
            return std::nullopt;
        }
        merkle_child_info_t const *child =
            &node->children()[merkle_child_index(node, nibble)];

        while (pi < 64) {
            nibble = get_nibble(key.data(), pi);

            if (child->path_len() == pi) {
                // read next node from disk if not in memory
                if (!child->next) {
                    // TODO: use io_uring blocking wait read
                    const_cast<merkle_child_info_t *>(child)->next = read_node(
                        io_->get_rd_fd(), child->fnext(), child->path_len());
                }
                node = child->next.get();

                // go to next node's matching branch
                if (!(node->valid_mask & 1u << nibble)) {
                    return std::nullopt;
                }
                child = &node->children()[merkle_child_index(node, nibble)];
            }
            else if (nibble != get_nibble(child->path, pi)) {
                return std::nullopt;
            }
            // nibble is matched
            ++pi;
        }
        return byte_string_view(child->data.get(), child->data_len());
    }

public:
    std::optional<byte_string_view> read(byte_string_view const &key)
    {
        if (!root_) {
            root_ = get_new_merkle_node(0, 0);
        }
        return read_helper(key, root_.get());
    }

    std::optional<byte_string>
    read_history(byte_string const &key, uint64_t const block_id)
    {
        // get historical root of block block_id
        auto root_off = index_->get_history_root_off(block_id);
        if (!root_off.has_value()) {
            return {};
        }
        merkle_node_ptr root = read_node(io_->get_rd_fd(), root_off.value());
        // call read_helper on that root;
        auto opt_res_view = read_helper(key, root.get());
        auto res = opt_res_view.has_value()
                       ? std::optional<byte_string>(opt_res_view.value())
                       : std::nullopt;
        return res;
    }

    constexpr merkle_node_t *get_root() const
    {
        return root_.get();
    }

    constexpr AsyncIO &get_io() const
    {
        return *io_;
    }

    void set_root(merkle_node_ptr root)
    {
        root_ = std::move(root);
    }

    ////////////////////////////////////////////////////////////////////
    // TODO: Merkle Proof Implementation
    ////////////////////////////////////////////////////////////////////
};

static_assert(sizeof(MerkleTrie) == 48);
static_assert(alignof(MerkleTrie) == 8);

MONAD_TRIE_NAMESPACE_END

#if (__GNUC__ == 12 || __GNUC__ == 13) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif
