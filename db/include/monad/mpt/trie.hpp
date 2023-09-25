#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

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

struct Compute;
class Node;

struct async_write_node_result
{
    file_offset_t offset_written_to;
    unsigned bytes_appended;
    MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state;
};
struct write_operation_io_receiver
{
    // Node *parent{nullptr};
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        result<std::span<const std::byte>> res)
    {
        MONAD_ASSERT(res);
        // TODO: when adding upsert_sender
        // if (parent->current_process_updates_sender_ != nullptr) {
        //     parent->current_process_updates_sender_
        //         ->notify_write_operation_completed_(rawstate);
        // }
    }
    void reset() {}
};

using node_writer_unique_ptr_type =
    MONAD_ASYNC_NAMESPACE::AsyncIO::connected_operation_unique_ptr_type<
        MONAD_ASYNC_NAMESPACE::write_single_buffer_sender,
        write_operation_io_receiver>;

struct UpdateAux
{
    Compute &comp;
    MONAD_ASYNC_NAMESPACE::AsyncIO *io{nullptr};
    node_writer_unique_ptr_type node_writer{};

    UpdateAux(
        Compute &comp_, MONAD_ASYNC_NAMESPACE::AsyncIO *io_ = nullptr,
        file_offset_t root_off = 0)
        : comp(comp_)
        , node_writer(node_writer_unique_ptr_type{})
    {
        if (io_) {
            set_io(io_, root_off);
        }
    }

    void set_io(MONAD_ASYNC_NAMESPACE::AsyncIO *io_, file_offset_t root_off = 0)
    {
        io = io_;
        node_writer =
            io ? io->make_connected(
                     MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                         round_up_align<DISK_PAGE_BITS>(root_off),
                         {(const std::byte *)nullptr,
                          MONAD_ASYNC_NAMESPACE::AsyncIO::WRITE_BUFFER_SIZE}},
                     write_operation_io_receiver{})
               : node_writer_unique_ptr_type{};
    }

    constexpr bool is_in_memory() const noexcept
    {
        return io == nullptr;
    }

    constexpr bool is_on_disk() const noexcept
    {
        return io != nullptr;
    }
};

async_write_node_result async_write_node(
    MONAD_ASYNC_NAMESPACE::AsyncIO &io,
    node_writer_unique_ptr_type &node_writer, Node *node);
// invoke at the end of each block upsert
async_write_node_result
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode);

//! update function interafaces
node_ptr upsert(UpdateAux &update_aux, Node *const old, UpdateList &&updates);

// TODO: have fine() backed by disk, too
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