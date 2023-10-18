#pragma once

#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

#include <monad/core/unordered_map.hpp>

#include <boost/fiber/future.hpp>

#include <cstdint>
#include <list>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute;
class Node;

struct write_operation_io_receiver
{
    // Node *parent{nullptr};
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        MONAD_ASYNC_NAMESPACE::result<std::span<const std::byte>> res)
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

using MONAD_ASYNC_NAMESPACE::receiver;
template <receiver Receiver>
struct read_update_sender : MONAD_ASYNC_NAMESPACE::read_single_buffer_sender
{
    read_update_sender(Receiver const &receiver)
        : read_single_buffer_sender(
              receiver.rd_offset, {(std::byte *)nullptr /*set by AsyncIO for
              us*/, receiver.bytes_to_read})
    {
    }
};

struct async_write_node_result
{
    file_offset_t offset_written_to;
    unsigned bytes_appended;
    MONAD_ASYNC_NAMESPACE::erased_connected_operation *io_state;
};
async_write_node_result async_write_node(
    MONAD_ASYNC_NAMESPACE::AsyncIO &, node_writer_unique_ptr_type &, Node *);

// \struct Auxiliaries for triedb update
struct UpdateAux
{
    Compute &comp;
    MONAD_ASYNC_NAMESPACE::AsyncIO *io{nullptr};
    node_writer_unique_ptr_type node_writer{};
    /* Note on list dimension: when using nested update list with level-based
     * caching in trie, need to specify which dimension in the nested list to
     * apply cache rule on. We keep all nodes before that dimension during batch
     * upsert, apply cache rule starting that dimension, and always evict nodes
     * when larger than that dimension */
    unsigned const list_dim_to_apply_cache{1};
    unsigned current_list_dim{0};

    UpdateAux(
        Compute &comp_, MONAD_ASYNC_NAMESPACE::AsyncIO *io_ = nullptr,
        unsigned const list_dim_to_apply_cache_ = 1, file_offset_t root_off = 0)
        : comp(comp_)
        , node_writer(node_writer_unique_ptr_type{})
        , list_dim_to_apply_cache(list_dim_to_apply_cache_)
        , current_list_dim{0}
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
static_assert(sizeof(UpdateAux) == 32);
static_assert(alignof(UpdateAux) == 8);

// batch upsert, updates can be nested
node_ptr upsert(UpdateAux &, Node *old, UpdateList &&);

//////////////////////////////////////////////////////////////////////////////

enum class find_result : uint8_t
{
    unknown,
    success,
    root_node_is_null_failure,
    key_mismatch_failure,
    branch_not_exist_failure,
    key_ends_ealier_than_node_failure
};
using find_result_type = std::pair<Node *, find_result>;

// The request type to put to the fiber buffered channel for triedb thread to
// work on
struct find_request_t
{
    ::boost::fibers::promise<find_result_type> *promise{nullptr};
    Node *root{nullptr};
    byte_string_view key{};
    std::optional<unsigned> node_pi{std::nullopt};
};
static_assert(sizeof(find_request_t) == 40);
static_assert(alignof(find_request_t) == 8);
static_assert(std::is_trivially_copyable_v<find_request_t> == true);

namespace detail
{
    // Type for intermediate pending requests that pends on an inflight read. It
    // is a pair of the search key and a promise associated with the initial
    // find_request_t.
    using pending_request_t = std::pair<
        NibblesView const, ::boost::fibers::promise<find_result_type> *const>;
    static_assert(std::is_trivially_copyable_v<pending_request_t> == true);
}
using inflight_map_t =
    unordered_dense_map<file_offset_t, std::list<detail::pending_request_t>>;

//! \warning this is not threadsafe, should only be called from triedb thread
// during execution, DO NOT invoke it directly from a transaction fiber, as is
// not race free.
void find_notify_fiber_future(
    MONAD_ASYNC_NAMESPACE::AsyncIO &, inflight_map_t &inflights,
    find_request_t);

/*! \brief Copy a leaf node under prefix `src` to prefix `dest`. Invoked before
committing block updates to triedb. By copy we mean everything other than
relpath. When copying children over, also remove the original's child pointers
to avoid dup referencing. For on-disk trie deallocate nodes under prefix `src`
after copy is done when the node is the only in-memory child of its parent. */
node_ptr copy_node(
    UpdateAux &, node_ptr root, byte_string_view src, byte_string_view dest);

/*! \brief blocking find node indexed by key from root, It works for bothon-disk
and in-memory trie. When node along key is not yet in memory, it load node
through blocking read.
 \warning Should only invoke it from the triedb owning
thread, as no synchronization is provided, and user code should make sure no
other place is modifying trie. */
find_result_type find_blocking(
    int fd, Node *root, byte_string_view key,
    std::optional<unsigned> opt_node_pi = std::nullopt);

// helper
inline constexpr unsigned num_pages(file_offset_t const offset, unsigned bytes)
{
    auto rd_offset = round_down_align<DISK_PAGE_BITS>(offset);
    bytes += offset - rd_offset;
    return (bytes + DISK_PAGE_SIZE - 1) >> DISK_PAGE_BITS;
}

MONAD_MPT_NAMESPACE_END
