#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>
#include <monad/trie/config.hpp>
#include <monad/trie/index.hpp>
#include <monad/trie/node_helper.hpp>
#include <monad/trie/request.hpp>
#include <monad/trie/tnode.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

static_assert(
    MONAD_ASYNC_NAMESPACE::AsyncIO::READ_BUFFER_SIZE >=
        round_up_align<DISK_PAGE_BITS>(
            uint16_t(MAX_DISK_NODE_SIZE + DISK_PAGE_SIZE)),
    "AsyncIO::READ_BUFFER_SIZE needs to be raised");
template <class T>
using result = MONAD_ASYNC_NAMESPACE::result<T>;
using MONAD_ASYNC_NAMESPACE::errc;
using MONAD_ASYNC_NAMESPACE::failure;
using MONAD_ASYNC_NAMESPACE::posix_code;
using MONAD_ASYNC_NAMESPACE::success;

static const byte_string empty_trie_hash = [] {
    using namespace ::monad::literals;
    return 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex;
}();

class MerkleTrie final
{
public:
    class process_updates_sender;
    friend class process_updates_sender;

private:
    struct write_operation_io_receiver_;
    friend struct write_operation_io_receiver_;
    struct write_operation_io_receiver_
    {
        MerkleTrie *parent{nullptr};
        void set_value(
            MONAD_ASYNC_NAMESPACE::erased_connected_operation *rawstate,
            result<std::span<const std::byte>> res)
        {
            MONAD_ASSERT(res);
            if (parent->current_process_updates_sender_ != nullptr) {
                parent->current_process_updates_sender_
                    ->notify_write_operation_completed_(rawstate);
            }
        }
        void reset() {}
    };
    using node_writer_unique_ptr_type_ =
        MONAD_ASYNC_NAMESPACE::AsyncIO::connected_operation_unique_ptr_type<
            MONAD_ASYNC_NAMESPACE::write_single_buffer_sender,
            write_operation_io_receiver_>;

    merkle_node_ptr root_;
    std::shared_ptr<MONAD_ASYNC_NAMESPACE::AsyncIO> io_;
    node_writer_unique_ptr_type_ node_writer_;
    std::shared_ptr<index_t> index_;
    const int cache_levels_;
    const bool is_account_;

    process_updates_sender *current_process_updates_sender_{nullptr};

    node_writer_unique_ptr_type_
    replace_node_writer_(size_t bytes_yet_to_be_appended_to_existing = 0)
    {
        auto ret = std::move(node_writer_);
        node_writer_ = io_->make_connected(
            MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                ret->sender().offset() + ret->sender().written_buffer_bytes() +
                    bytes_yet_to_be_appended_to_existing,
                {(const std::byte *)nullptr,
                 MONAD_ASYNC_NAMESPACE::AsyncIO::WRITE_BUFFER_SIZE}},
            write_operation_io_receiver_{this});
        return ret;
    }

public:
    MerkleTrie(
        bool const is_account, file_offset_t block_off,
        merkle_node_ptr root = {},
        std::shared_ptr<MONAD_ASYNC_NAMESPACE::AsyncIO> io = {},
        std::shared_ptr<index_t> index = {}, int cache_levels = 5)
        : root_(std::move(root))
        , io_(std::move(io))
        , node_writer_(
              io_ ? io_->make_connected(
                        MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                            round_up_align<DISK_PAGE_BITS>(block_off),
                            {(const std::byte *)nullptr,
                             MONAD_ASYNC_NAMESPACE::AsyncIO::
                                 WRITE_BUFFER_SIZE}},
                        write_operation_io_receiver_{this})
                  : node_writer_unique_ptr_type_{})
        , index_(std::move(index))
        , cache_levels_(cache_levels)
        , is_account_(is_account)
    {
    }

    MerkleTrie(
        bool const is_account, file_offset_t const root_off,
        std::shared_ptr<MONAD_ASYNC_NAMESPACE::AsyncIO> io,
        std::shared_ptr<index_t> index = {}, int cache_levels = 5)
        : root_(
              root_off == INVALID_OFFSET ? get_new_merkle_node(0, 0)
                                         : read_node(io->get_rd_fd(), root_off))
        , io_(std::move(io))
        , node_writer_(
              io_ ? io_->make_connected(
                        MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                            round_up_align<DISK_PAGE_BITS>(root_off),
                            {(const std::byte *)nullptr,
                             MONAD_ASYNC_NAMESPACE::AsyncIO::
                                 WRITE_BUFFER_SIZE}},
                        write_operation_io_receiver_{this})
                  : node_writer_unique_ptr_type_{})
        , index_(std::move(index))
        , cache_levels_(cache_levels)
        , is_account_(is_account)
    {
    }

    MerkleTrie(const MerkleTrie &) = delete;
    MerkleTrie(MerkleTrie &&) = delete;
    MerkleTrie &operator=(const MerkleTrie &) = delete;
    MerkleTrie &operator=(MerkleTrie &&) = delete;
    ~MerkleTrie() = default;

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

    class process_updates_sender
    {
        friend struct write_operation_io_receiver_;
        MerkleTrie *parent_;
        monad::mpt::UpdateList &updates_;
        uint64_t const block_id_;

        MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state_{nullptr},
            *awaiting_block_completion_{nullptr};
        merkle_node_ptr prev_root_;
        tnode_t::unique_ptr_type root_tnode_;

        static void all_updates_written_indirect_(tnode_t *, void *p) noexcept
        {
            ((process_updates_sender *)p)
                ->notify_write_operation_completed_(nullptr);
        }

        // A write buffer just completed flushing, or all updates just got
        // written (nullptr)
        void notify_write_operation_completed_(
            MONAD_ASYNC_NAMESPACE::erased_connected_operation
                *io_state) noexcept
        {
            if (!root_tnode_ || root_tnode_->npending() == 0) {
                if (parent_->io_) {
                    if (awaiting_block_completion_ == nullptr ||
                        awaiting_block_completion_ != io_state) {
                        // flush() will block if there are blocks written
                        // awaiting notification of completion. So do nothing
                        // until those complete, we'll get called back here
                        // every time one of those completes.
                        if (parent_->io_->writes_in_flight() > 0) {
                            return;
                        }
                        // Write out the remaining pending block of writes and
                        // the new root node. This should not block.
                        auto root_off = parent_->flush_and_write_new_root_node(
                            root_tnode_->node);
                        if (root_off.offset_written_to != INVALID_OFFSET) {
                            if (parent_->index_) {
                                parent_->index_->write_record(
                                    block_id_, root_off.offset_written_to);
                            }
                            awaiting_block_completion_ = root_off.io_state;
                            return;
                        }
                    }
                    // We can complete now
                    awaiting_block_completion_ = nullptr;
                }
                io_state_->completed(success());
            }
        }

    public:
        // Probably should become result<merkle_node_ptr> in the near future?
        using result_type = result<merkle_node_t *>;

        process_updates_sender(
            MerkleTrie *trie, monad::mpt::UpdateList &updates,
            uint64_t const block_id = 0)
            : parent_(trie)
            , updates_(updates)
            , block_id_(block_id)
        {
        }
        result<void>
        operator()(MONAD_ASYNC_NAMESPACE::erased_connected_operation
                       *io_state) noexcept
        {
            MONAD_ASSERT(parent_->current_process_updates_sender_ == nullptr);
            parent_->current_process_updates_sender_ = this;
            io_state_ = io_state;
            prev_root_ = parent_->root_ ? std::move(parent_->root_)
                                        : get_new_merkle_node(0, 0);

            Request::unique_ptr_type updateq =
                Request::make(std::move(updates_));
            SubRequestInfo requests;
            updateq = updateq->split_into_subqueues(
                std::move(updateq), &requests, /*not root*/ false);

            root_tnode_ = get_new_tnode(
                &process_updates_sender::all_updates_written_indirect_, this);
            parent_->root_ = parent_->do_update(
                prev_root_.get(), requests, root_tnode_.get());
            return success();
        }
        result_type completed(
            MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
            result<void> res)
        {
            BOOST_OUTCOME_TRY(std::move(res));
            MONAD_ASSERT(parent_->current_process_updates_sender_ == this);
            parent_->current_process_updates_sender_ = nullptr;
            return parent_->root_.get();
        }
    };

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
                child->path_len() == 64 && is_account_ ? ROOT_OFFSET_SIZE : 0,
                dest,
                child->path_len() == 64);
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

    MONAD_ASYNC_NAMESPACE::AsyncIO &get_io() const
    {
        return *io_;
    }

    void set_root(merkle_node_ptr root)
    {
        root_ = std::move(root);
    }

    constexpr bool is_account() const
    {
        return is_account_;
    }

    struct async_write_node_result
    {
        file_offset_t offset_written_to;
        unsigned bytes_appended;
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state;
    };
    async_write_node_result async_write_node(merkle_node_t *node);

    // invoke at the end of each block
    async_write_node_result flush_and_write_new_root_node(merkle_node_t *root);

    ////////////////////////////////////////////////////////////////////
    // TODO: Merkle Proof Implementation
    ////////////////////////////////////////////////////////////////////
};

static_assert(sizeof(MerkleTrie) == 64);
static_assert(alignof(MerkleTrie) == 8);

MONAD_TRIE_NAMESPACE_END
