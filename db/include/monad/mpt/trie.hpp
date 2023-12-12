#pragma once

#include <monad/mpt/compute.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/detail/db_metadata.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

#include <monad/core/unordered_map.hpp>

#include <boost/container/devector.hpp>
#include <boost/fiber/future.hpp>

#include <cstdint>
#include <list>

MONAD_MPT_NAMESPACE_BEGIN

class Node;

struct write_operation_io_receiver
{
    // Node *parent{nullptr};
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        MONAD_ASYNC_NAMESPACE::result<std::span<std::byte const>> res)
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

chunk_offset_t
async_write_node_set_spare(UpdateAux &aux, Node &node, bool is_fast);

// \class Auxiliaries for triedb update
class UpdateAux
{
    detail::db_metadata *db_metadata_[2]{
        nullptr, nullptr}; // two copies, to prevent sudden process
                           // exits making the DB irretrievable

    ::boost::container::devector<uint32_t> insertion_count_to_chunk_id_[3];

    void reset_node_writers();

public:
    enum class chunk_list : uint8_t
    {
        free = 0,
        fast = 1,
        slow = 2
    };

    detail::db_metadata const *db_metadata() const noexcept
    {
        return db_metadata_[0];
    }

    uint32_t chunk_id_from_insertion_count(
        chunk_list list_type,
        detail::unsigned_20 insertion_count) const noexcept;

    std::pair<chunk_list, detail::unsigned_20>
    chunk_list_and_age(uint32_t idx) const noexcept;

    void append(chunk_list list, uint32_t idx) noexcept;
    void remove(uint32_t idx) noexcept;

    void advance_offsets_to(
        chunk_offset_t const root_offset, chunk_offset_t const fast_offset,
        chunk_offset_t const slow_offset) noexcept
    {
        auto do_ = [&](detail::db_metadata *m) {
            m->advance_offsets_to_(root_offset, fast_offset, slow_offset);
        };
        do_(db_metadata_[0]);
        do_(db_metadata_[1]);
    }

    // WARNING: This is destructive
    void rewind_to_match_offsets();

    MONAD_ASYNC_NAMESPACE::AsyncIO *io{nullptr};
    node_writer_unique_ptr_type node_writer_fast{};
    node_writer_unique_ptr_type node_writer_slow{};

    UpdateAux(MONAD_ASYNC_NAMESPACE::AsyncIO *io_ = nullptr)
        : node_writer_fast(node_writer_unique_ptr_type{})
        , node_writer_slow(node_writer_unique_ptr_type{})
    {
        if (io_) {
            set_io(io_);
        }
    }

    ~UpdateAux();

    void set_io(MONAD_ASYNC_NAMESPACE::AsyncIO *io_);

    bool alternate_slow_fast_writer{false};
    bool can_write_to_fast{true};

    // WARNING: for unit testing only
    // DO NOT invoke it outside of unit test
    void alternate_slow_fast_node_writer_unit_testing_only(bool alternate)
    {
        alternate_slow_fast_writer = alternate;
    }

    constexpr bool is_in_memory() const noexcept
    {
        return io == nullptr;
    }

    constexpr bool is_on_disk() const noexcept
    {
        return io != nullptr;
    }

    chunk_offset_t get_root_offset() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata_[0]->root_offset;
    }

    chunk_offset_t get_start_of_wip_slow_offset() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata_[0]->start_of_wip_slow_offset;
    }

    chunk_offset_t get_start_of_wip_fast_offset() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata_[0]->start_of_wip_fast_offset;
    }

    file_offset_t get_lower_bound_free_space() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata_[0]->capacity_in_free_list;
    }
};

static_assert(sizeof(UpdateAux) == 144);
static_assert(alignof(UpdateAux) == 8);

// batch upsert, updates can be nested
Node::UniquePtr
upsert(UpdateAux &, TrieStateMachine &, Node::UniquePtr old, UpdateList &&);

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
    std::optional<unsigned> node_prefix_index{std::nullopt};
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

using inflight_map_t = unordered_dense_map<
    chunk_offset_t, std::list<detail::pending_request_t>,
    chunk_offset_t_hasher>;

//! \warning this is not threadsafe, should only be called from triedb thread
// during execution, DO NOT invoke it directly from a transaction fiber, as is
// not race free.
void find_notify_fiber_future(
    MONAD_ASYNC_NAMESPACE::AsyncIO &, inflight_map_t &inflights,
    find_request_t);

/*! \brief Copy a leaf node under prefix `src` to prefix `dest`. Invoked before
committing block updates to triedb. By copy we mean everything other than
path. When copying children over, also remove the original's child pointers
to avoid dup referencing. For on-disk trie deallocate nodes under prefix `src`
after copy is done when the node is the only in-memory child of its parent.
Note that we handle the case where `dest` is pre-existed in trie. */
Node::UniquePtr
copy_node(UpdateAux &, Node::UniquePtr root, NibblesView src, NibblesView dest);

/*! \brief blocking find node indexed by key from root, It works for bothon-disk
and in-memory trie. When node along key is not yet in memory, it load node
through blocking read.
 \warning Should only invoke it from the triedb owning
thread, as no synchronization is provided, and user code should make sure no
other place is modifying trie. */
find_result_type find_blocking(
    MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node *root, NibblesView key,
    std::optional<unsigned> opt_node_prefix_index = std::nullopt);

Nibbles
find_min_key_blocking(MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node &root);
Nibbles
find_max_key_blocking(MONAD_ASYNC_NAMESPACE::storage_pool *pool, Node &root);

// helper
inline constexpr unsigned num_pages(file_offset_t const offset, unsigned bytes)
{
    auto const rd_offset = round_down_align<DISK_PAGE_BITS>(offset);
    bytes += static_cast<unsigned>(offset - rd_offset);
    return (bytes + DISK_PAGE_SIZE - 1) >> DISK_PAGE_BITS;
}

MONAD_MPT_NAMESPACE_END
