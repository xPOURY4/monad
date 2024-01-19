#pragma once

#include <monad/mpt/compute.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/detail/collected_stats.hpp>
#include <monad/mpt/detail/db_metadata.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/state_machine.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>

#include <monad/core/unordered_map.hpp>

#include <boost/container/devector.hpp>
#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/fiber/future.hpp>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

#include <cstdint>
#include <list>

MONAD_MPT_NAMESPACE_BEGIN

class Node;

struct write_operation_io_receiver
{
    // Node *parent{nullptr};
    void set_value(
        MONAD_ASYNC_NAMESPACE::erased_connected_operation *,
        MONAD_ASYNC_NAMESPACE::write_single_buffer_sender::result_type res)
    {
        MONAD_ASSERT(res);
        res.assume_value()
            .get()
            .reset(); // release i/o buffer before initiating other work
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

struct read_short_update_sender
    : MONAD_ASYNC_NAMESPACE::read_single_buffer_sender
{
    template <receiver Receiver>
    constexpr read_short_update_sender(Receiver const &receiver)
        : read_single_buffer_sender(receiver.rd_offset, receiver.bytes_to_read)
    {
        MONAD_DEBUG_ASSERT(
            receiver.bytes_to_read <=
            MONAD_ASYNC_NAMESPACE::AsyncIO::READ_BUFFER_SIZE);
    }
};

class read_long_update_sender
    : public MONAD_ASYNC_NAMESPACE::read_multiple_buffer_sender
{
    MONAD_ASYNC_NAMESPACE::read_multiple_buffer_sender::buffer_type buffer_;

public:
    template <receiver Receiver>
    read_long_update_sender(Receiver const &receiver)
        : MONAD_ASYNC_NAMESPACE::read_multiple_buffer_sender(
              receiver.rd_offset, {&buffer_, 1})
        , buffer_(
              (std::byte *)aligned_alloc(
                  DISK_PAGE_SIZE, receiver.bytes_to_read),
              receiver.bytes_to_read)
    {
        MONAD_DEBUG_ASSERT(
            receiver.bytes_to_read >
            MONAD_ASYNC_NAMESPACE::AsyncIO::READ_BUFFER_SIZE);
        MONAD_ASSERT(buffer_.data() != nullptr);
    }

    read_long_update_sender(read_long_update_sender &&o) noexcept
        : MONAD_ASYNC_NAMESPACE::read_multiple_buffer_sender(std::move(o))
        , buffer_(o.buffer_)
    {
        o.buffer_ = {};
    }

    read_long_update_sender &operator=(read_long_update_sender &&o) noexcept

    {
        if (this != &o) {
            this->~read_long_update_sender();
            new (this) read_long_update_sender(std::move(o));
        }
        return *this;
    }

    ~read_long_update_sender()
    {
        if (buffer_.data() != nullptr) {
            ::free(buffer_.data());
            buffer_ = {};
        }
    }
};

virtual_chunk_offset_t
async_write_node_set_spare(UpdateAux &aux, Node &node, bool is_fast);

node_writer_unique_ptr_type replace_node_writer(
    UpdateAux &, node_writer_unique_ptr_type &,
    size_t bytes_yet_to_be_appended_to_existing = 0,
    size_t bytes_to_write_to_new_writer = 0);

// \class Auxiliaries for triedb update
class UpdateAux
{
    uint32_t initial_insertion_count_on_pool_creation_{0};

    detail::db_metadata *db_metadata_[2]{
        nullptr, nullptr}; // two copies, to prevent sudden process
                           // exits making the DB irretrievable

    ::boost::container::devector<uint32_t> insertion_count_to_chunk_id_[3];

    /******** State Histories ********/
    struct state_disk_info_t
    {
        uint64_t block_id{0};
        compact_virtual_chunk_offset_t min_offset_fast{
            MIN_COMPACT_VIRTUAL_OFFSET};
        compact_virtual_chunk_offset_t min_offset_slow{
            MIN_COMPACT_VIRTUAL_OFFSET};
        compact_virtual_chunk_offset_t max_offset_fast{
            MIN_COMPACT_VIRTUAL_OFFSET};
        compact_virtual_chunk_offset_t max_offset_slow{
            MIN_COMPACT_VIRTUAL_OFFSET};
    };

    void reset_node_writers();

    void advance_compact_offsets(state_disk_info_t);

    void free_compacted_chunks();

    ::boost::container::devector<state_disk_info_t> state_histories;

    /******** Compaction ********/
    uint32_t remove_chunks_before_count_fast_{0};
    uint32_t remove_chunks_before_count_slow_{0};
    // speed control var
    compact_virtual_chunk_offset_t last_block_end_offset_fast_{
        MIN_COMPACT_VIRTUAL_OFFSET};
    compact_virtual_chunk_offset_t last_block_end_offset_slow_{
        MIN_COMPACT_VIRTUAL_OFFSET};
    compact_virtual_chunk_offset_t last_block_disk_growth_fast_{
        MIN_COMPACT_VIRTUAL_OFFSET};
    compact_virtual_chunk_offset_t last_block_disk_growth_slow_{
        MIN_COMPACT_VIRTUAL_OFFSET};
    // compaction range
    compact_virtual_chunk_offset_t compact_offset_range_fast_{
        MIN_COMPACT_VIRTUAL_OFFSET};
    compact_virtual_chunk_offset_t compact_offset_range_slow_{
        MIN_COMPACT_VIRTUAL_OFFSET};

public:
    compact_virtual_chunk_offset_t compact_offset_fast{
        MIN_COMPACT_VIRTUAL_OFFSET};
    compact_virtual_chunk_offset_t compact_offset_slow{
        MIN_COMPACT_VIRTUAL_OFFSET};

    // On disk stuff
    MONAD_ASYNC_NAMESPACE::AsyncIO *io{nullptr};
    node_writer_unique_ptr_type node_writer_fast{};
    node_writer_unique_ptr_type node_writer_slow{};

    // currently maintain a fixed len history
    static constexpr unsigned block_history_len = 200;

    bool alternate_slow_fast_writer{false};
    bool can_write_to_fast{true};

    UpdateAux(MONAD_ASYNC_NAMESPACE::AsyncIO *io_ = nullptr)
    {
        if (io_) {
            set_io(io_);
        }
    }

    ~UpdateAux();

    void set_io(MONAD_ASYNC_NAMESPACE::AsyncIO *);

    void restore_state_history_disk_infos(
        Node &root, std::optional<uint64_t> const max_block_id = std::nullopt);

    //! Copy state from last block to new block, erase outdated history block
    //! if any, do compaction if specified, and then upsert
    //! `updates` should include everything nested under block number
    Node::UniquePtr upsert_with_fixed_history_len(
        Node::UniquePtr prev_root, StateMachine &, UpdateList &&,
        uint64_t block_id, bool compaction);

#if MONAD_MPT_COLLECT_STATS
    detail::TrieUpdateCollectedStats stats;
#endif
    // collect and print trie update stats
    void reset_stats();
    void collect_number_nodes_created_stats();
    void collect_compaction_read_stats(
        virtual_chunk_offset_t node_offset, unsigned bytes_to_read);
    void collect_compacted_nodes_stats(
        compact_virtual_chunk_offset_t subtrie_min_offset_fast,
        compact_virtual_chunk_offset_t subtrie_min_offset_slow);
    void collect_compacted_nodes_from_to_stats(
        virtual_chunk_offset_t node_offset, bool rewrite_to_fast);
    void print_update_stats();

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

    // translate between virtual and physical addresses chunk_offset_t
    virtual_chunk_offset_t physical_to_virtual(chunk_offset_t) const noexcept;
    chunk_offset_t virtual_to_physical(virtual_chunk_offset_t) const noexcept;

    // age is relative to the beginning chunk's count
    std::pair<chunk_list, detail::unsigned_20>
    chunk_list_and_age(uint32_t idx) const noexcept;

    void append(chunk_list list, uint32_t idx) noexcept;
    void remove(uint32_t idx) noexcept;

    // The following two functions should only be invoked after completing a
    // block commit
    void advance_offsets_to(
        chunk_offset_t root_offset, chunk_offset_t fast_offset,
        chunk_offset_t slow_offset) noexcept;
    void update_slow_fast_ratio_metadata() noexcept;

    // WARNING: This is destructive
    void rewind_to_match_offsets();

    void set_initial_insertion_count_unit_testing_only(uint32_t count)
    {
        initial_insertion_count_on_pool_creation_ = count;
    }

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
        return db_metadata()->db_offsets.root_offset;
    }

    chunk_offset_t get_start_of_wip_fast_offset() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata()->db_offsets.start_of_wip_offset_fast;
    }

    chunk_offset_t get_start_of_wip_slow_offset() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata()->db_offsets.start_of_wip_offset_slow;
    }

    file_offset_t get_lower_bound_free_space() const noexcept
    {
        MONAD_ASSERT(this->is_on_disk());
        return db_metadata()->capacity_in_free_list;
    }

    uint32_t num_chunks(chunk_list const list) const noexcept;

    uint64_t min_block_id_in_history() const noexcept
    {
        MONAD_ASSERT(state_histories.size());
        return state_histories.front().block_id;
    }

    uint64_t max_block_id_in_history() const noexcept
    {
        MONAD_ASSERT(state_histories.size());
        return state_histories.back().block_id;
    }

    uint64_t next_block_id() const noexcept
    {
        return state_histories.empty() ? 0 : max_block_id_in_history() + 1;
    }
};

static_assert(
    sizeof(UpdateAux) ==
    224 + MONAD_MPT_COLLECT_STATS * sizeof(detail::TrieUpdateCollectedStats));
static_assert(alignof(UpdateAux) == 8);

// batch upsert, updates can be nested
Node::UniquePtr
upsert(UpdateAux &, StateMachine &, Node::UniquePtr old, UpdateList &&);

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
    virtual_chunk_offset_t, std::list<detail::pending_request_t>,
    virtual_chunk_offset_t_hasher>;

//! \warning this is not threadsafe, should only be called from triedb thread
// during execution, DO NOT invoke it directly from a transaction fiber, as is
// not race free.
void find_notify_fiber_future(
    UpdateAux &, inflight_map_t &inflights, find_request_t);

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
    UpdateAux const &, Node *root, NibblesView key,
    std::optional<unsigned> opt_node_prefix_index = std::nullopt);

Nibbles find_min_key_blocking(UpdateAux const &, Node &root);
Nibbles find_max_key_blocking(UpdateAux const &, Node &root);

// helper
inline constexpr unsigned num_pages(file_offset_t const offset, unsigned bytes)
{
    auto const rd_offset = round_down_align<DISK_PAGE_BITS>(offset);
    bytes += static_cast<unsigned>(offset - rd_offset);
    return (bytes + DISK_PAGE_SIZE - 1) >> DISK_PAGE_BITS;
}

inline std::pair<compact_virtual_chunk_offset_t, compact_virtual_chunk_offset_t>
calc_min_offsets(
    Node &node,
    virtual_chunk_offset_t node_virtual_offset = INVALID_VIRTUAL_OFFSET)
{
    auto fast_ret = INVALID_COMPACT_VIRTUAL_OFFSET;
    auto slow_ret = INVALID_COMPACT_VIRTUAL_OFFSET;
    if (node_virtual_offset != INVALID_VIRTUAL_OFFSET) {
        auto const truncated_offset =
            compact_virtual_chunk_offset_t{node_virtual_offset};
        if (node_virtual_offset.in_fast_list()) {
            fast_ret = truncated_offset;
        }
        else {
            slow_ret = truncated_offset;
        }
    }
    for (unsigned i = 0; i < node.number_of_children(); ++i) {
        fast_ret = std::min(fast_ret, node.min_offset_fast(i));
        slow_ret = std::min(slow_ret, node.min_offset_slow(i));
    }
    // if ret is valid
    if (fast_ret != INVALID_COMPACT_VIRTUAL_OFFSET) {
        MONAD_ASSERT(fast_ret < (1u << 31));
    }
    if (slow_ret != INVALID_COMPACT_VIRTUAL_OFFSET) {
        MONAD_ASSERT(slow_ret < (1u << 31));
    }
    return {fast_ret, slow_ret};
}

MONAD_MPT_NAMESPACE_END
