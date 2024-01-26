#include <monad/async/config.hpp>
#include <monad/core/assert.h>
#include <monad/core/small_prng.hpp>
#include <monad/core/unaligned.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/util.hpp>

#include <monad/async/detail/start_lifetime_as_polyfill.hpp>
#include <monad/mpt/detail/unsigned_20.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

// Define to avoid randomisation of free list chunks on pool creation
// This can be useful to discover bugs in code which assume chunks are
// consecutive
// #define MONAD_MPT_INITIALIZE_POOL_WITH_CONSECUTIVE_CHUNKS 1

uint32_t UpdateAux::chunk_id_from_insertion_count(
    chunk_list list, detail::unsigned_20 insertion_count) const noexcept
{
    MONAD_ASSERT(is_on_disk());
    uint32_t idx = uint32_t(insertion_count);
    switch (list) {
    case chunk_list::free:
        idx -= uint32_t(db_metadata()->free_list_begin()->insertion_count());
        break;
    case chunk_list::fast:
        idx -= uint32_t(db_metadata()->fast_list_begin()->insertion_count());
        break;
    case chunk_list::slow:
        idx -= uint32_t(db_metadata()->slow_list_begin()->insertion_count());
        break;
    }
    auto const &map = insertion_count_to_chunk_id_[uint8_t(list)];
    if (idx >= map.size()) {
        return uint32_t(-1);
    }
    return map[idx];
}

virtual_chunk_offset_t
UpdateAux::physical_to_virtual(chunk_offset_t offset) const noexcept
{
    MONAD_ASSERT(offset.id < io->chunk_count());
    auto const *ci = db_metadata_[0]->at(offset.id);
    // should never invoke a translation for offset in free list
    MONAD_DEBUG_ASSERT(ci->in_fast_list || ci->in_slow_list);
    return {
        uint32_t(ci->insertion_count()),
        offset.offset,
        ci->in_fast_list,
        offset.spare & virtual_chunk_offset_t::max_spare};
}

chunk_offset_t
UpdateAux::virtual_to_physical(virtual_chunk_offset_t offset) const noexcept
{
    auto const id = chunk_id_from_insertion_count(
        offset.in_fast_list() ? chunk_list::fast : chunk_list::slow,
        offset.count);
    MONAD_ASSERT(id < io->chunk_count());
    return {id, offset.offset, offset.spare};
}

std::pair<UpdateAux::chunk_list, detail::unsigned_20>
UpdateAux::chunk_list_and_age(uint32_t idx) const noexcept
{
    MONAD_ASSERT(is_on_disk());
    auto const *ci = db_metadata_[0]->at(idx);
    std::pair<chunk_list, detail::unsigned_20> ret(
        chunk_list::free, ci->insertion_count());
    if (ci->in_fast_list) {
        ret.first = chunk_list::fast;
        ret.second -= db_metadata_[0]->fast_list_begin()->insertion_count();
    }
    else if (ci->in_slow_list) {
        ret.first = chunk_list::slow;
        ret.second -= db_metadata_[0]->slow_list_begin()->insertion_count();
    }
    else {
        ret.second -= db_metadata_[0]->free_list_begin()->insertion_count();
    }
    return ret;
}

void UpdateAux::append(chunk_list list, uint32_t idx) noexcept
{
    MONAD_ASSERT(is_on_disk());
    auto do_ = [&](detail::db_metadata *m) {
        switch (list) {
        case chunk_list::free:
            m->append_(m->free_list, m->at_(idx));
            break;
        case chunk_list::fast:
            m->append_(m->fast_list, m->at_(idx));
            break;
        case chunk_list::slow:
            m->append_(m->slow_list, m->at_(idx));
            break;
        }
    };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
    auto &map = insertion_count_to_chunk_id_[uint8_t(list)];
    map.emplace_back(idx);
    if (list == chunk_list::free) {
        auto chunk = io->storage_pool().chunk(storage_pool::seq, idx);
        auto capacity = chunk->capacity();
        MONAD_DEBUG_ASSERT(chunk->size() == 0);
        db_metadata_[0]->free_capacity_add_(capacity);
        db_metadata_[1]->free_capacity_add_(capacity);
    }
}

void UpdateAux::remove(uint32_t idx) noexcept
{
    MONAD_ASSERT(is_on_disk());
    bool const in_fast_list = db_metadata_[0]->at_(idx)->in_fast_list;
    bool const in_slow_list = db_metadata_[0]->at_(idx)->in_slow_list;
    bool const in_free_list = !in_fast_list && !in_slow_list;
    chunk_list const list =
        in_free_list ? chunk_list::free
                     : (in_fast_list ? chunk_list::fast : chunk_list::slow);
    auto do_ = [&](detail::db_metadata *m) { m->remove_(m->at_(idx)); };
    auto &map = insertion_count_to_chunk_id_[uint8_t(list)];
    MONAD_DEBUG_ASSERT(
        uint32_t(map.front()) == idx || uint32_t(map.back()) == idx);
    if (uint32_t(map.back()) == idx) {
        map.pop_back();
    }
    else {
        map.pop_front();
    }
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
    if (in_free_list) {
        auto chunk = io->storage_pool().chunk(storage_pool::seq, idx);
        auto capacity = chunk->capacity();
        MONAD_DEBUG_ASSERT(chunk->size() == 0);
        db_metadata_[0]->free_capacity_sub_(capacity);
        db_metadata_[1]->free_capacity_sub_(capacity);
    }
}

void UpdateAux::advance_offsets_to(
    chunk_offset_t const root_offset, chunk_offset_t const fast_offset,
    chunk_offset_t const slow_offset) noexcept
{
    MONAD_ASSERT(is_on_disk());
    auto do_ = [&](detail::db_metadata *m) {
        m->advance_offsets_to_(detail::db_metadata::db_offsets_info_t{
            root_offset,
            fast_offset,
            slow_offset,
            this->compact_offset_fast,
            this->compact_offset_slow,
            this->compact_offset_range_fast_,
            this->compact_offset_range_slow_});
    };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
}

void UpdateAux::update_slow_fast_ratio_metadata() noexcept
{
    MONAD_ASSERT(is_on_disk());
    auto ratio = (float)num_chunks(chunk_list::slow) /
                 (float)num_chunks(chunk_list::fast);
    auto do_ = [&](detail::db_metadata *m) {
        m->update_slow_fast_ratio_(ratio);
    };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
}

void UpdateAux::update_version_metadata(
    uint64_t const min_version, uint64_t const max_version) noexcept
{
    MONAD_ASSERT(is_on_disk());
    auto do_ = [&](detail::db_metadata *m) {
        m->update_version_info_(min_version, max_version);
    };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
}

void UpdateAux::rewind_to_match_offsets()
{
    MONAD_ASSERT(is_on_disk());
    // Free all chunks after fast_offset.id
    auto const fast_offset = db_metadata()->db_offsets.start_of_wip_offset_fast;
    auto const slow_offset = db_metadata()->db_offsets.start_of_wip_offset_slow;

    auto *ci = db_metadata_[0]->at(fast_offset.id);
    while (ci != db_metadata_[0]->fast_list_end()) {
        auto const idx = db_metadata_[0]->fast_list.end;
        remove(idx);
        io->storage_pool().chunk(storage_pool::seq, idx)->destroy_contents();
        append(chunk_list::free, idx);
    }
    auto fast_offset_chunk =
        io->storage_pool().chunk(storage_pool::seq, fast_offset.id);
    MONAD_ASSERT(fast_offset_chunk->try_trim_contents(fast_offset.offset));

    // Same for slow list
    auto *slow_ci = db_metadata_[0]->at(slow_offset.id);
    while (slow_ci != db_metadata_[0]->slow_list_end()) {
        auto const idx = db_metadata_[0]->slow_list.end;
        remove(idx);
        io->storage_pool().chunk(storage_pool::seq, idx)->destroy_contents();
        append(chunk_list::free, idx);
    }
    auto slow_offset_chunk =
        io->storage_pool().chunk(storage_pool::seq, slow_offset.id);
    MONAD_ASSERT(slow_offset_chunk->try_trim_contents(slow_offset.offset));

    // Reset node_writers offset to the same offsets in db_metadata
    reset_node_writers();
}

UpdateAux::~UpdateAux()
{
    if (io != nullptr) {
        auto const chunk_count = io->chunk_count();
        auto const map_size =
            sizeof(detail::db_metadata) +
            chunk_count * sizeof(detail::db_metadata::chunk_info_t);
        (void)::munmap(db_metadata_[0], map_size);
        (void)::munmap(db_metadata_[1], map_size);
    }
}

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
void UpdateAux::set_io(AsyncIO *io_)
{
    io = io_;
    auto const chunk_count = io->chunk_count();
    MONAD_ASSERT(chunk_count >= 3);
    auto const map_size =
        sizeof(detail::db_metadata) +
        chunk_count * sizeof(detail::db_metadata::chunk_info_t);
    auto cnv_chunk = io->storage_pool().activate_chunk(storage_pool::cnv, 0);
    auto fd = cnv_chunk->write_fd(0);
    db_metadata_[0] = start_lifetime_as<detail::db_metadata>(::mmap(
        nullptr,
        map_size,
        PROT_READ | PROT_WRITE,
        io->storage_pool().is_read_only() ? MAP_PRIVATE : MAP_SHARED,
        fd.first,
        off_t(fd.second)));
    MONAD_ASSERT(db_metadata_[0] != MAP_FAILED);
    db_metadata_[1] = start_lifetime_as<detail::db_metadata>(::mmap(
        nullptr,
        map_size,
        PROT_READ | PROT_WRITE,
        io->storage_pool().is_read_only() ? MAP_PRIVATE : MAP_SHARED,
        fd.first,
        off_t(fd.second + cnv_chunk->capacity() / 2)));
    MONAD_ASSERT(db_metadata_[1] != MAP_FAILED);
    // If the front copy vanished for some reason ...
    if (0 != memcmp(db_metadata_[0]->magic, "MND0", 4)) {
        if (0 == memcmp(db_metadata_[1]->magic, "MND0", 4)) {
            memcpy(db_metadata_[0], db_metadata_[1], map_size);
        }
    }
    // Replace any dirty copy with the non-dirty copy
    if (0 == memcmp(db_metadata_[0]->magic, "MND0", 4) &&
        0 == memcmp(db_metadata_[1]->magic, "MND0", 4)) {
        MONAD_ASSERT(
            !db_metadata_[0]->is_dirty().load(std::memory_order_acquire) ||
            !db_metadata_[1]->is_dirty().load(std::memory_order_acquire));
        if (db_metadata_[0]->is_dirty().load(std::memory_order_acquire)) {
            memcpy(db_metadata_[0], db_metadata_[1], map_size);
        }
        else if (db_metadata_[1]->is_dirty().load(std::memory_order_acquire)) {
            memcpy(db_metadata_[1], db_metadata_[0], map_size);
        }
    }
    if (0 != memcmp(db_metadata_[0]->magic, "MND0", 4)) {
        memset(db_metadata_[0], 0, map_size);
        MONAD_DEBUG_ASSERT((chunk_count & ~0xfffffU) == 0);
        db_metadata_[0]->chunk_info_count = chunk_count & 0xfffffU;
        memset(
            &db_metadata_[0]->free_list,
            0xff,
            sizeof(db_metadata_[0]->free_list));
        memset(
            &db_metadata_[0]->fast_list,
            0xff,
            sizeof(db_metadata_[0]->fast_list));
        memset(
            &db_metadata_[0]->slow_list,
            0xff,
            sizeof(db_metadata_[0]->slow_list));
        auto *chunk_info =
            start_lifetime_as_array<detail::db_metadata::chunk_info_t>(
                db_metadata_[0]->chunk_info, chunk_count);
        for (size_t n = 0; n < chunk_count; n++) {
            auto &ci = chunk_info[n];
            ci.prev_chunk_id = ci.next_chunk_id =
                detail::db_metadata::chunk_info_t::INVALID_CHUNK_ID;
        }
        memcpy(db_metadata_[1], db_metadata_[0], map_size);

        // Insert all chunks into the free list
        std::vector<uint32_t> chunks;
        chunks.reserve(chunk_count);
        for (uint32_t n = 0; n < chunk_count; n++) {
            auto chunk = io->storage_pool().chunk(storage_pool::seq, n);
            MONAD_ASSERT(chunk->size() == 0); // chunks must actually be free
            chunks.push_back(n);
        }
#if !MONAD_MPT_INITIALIZE_POOL_WITH_CONSECUTIVE_CHUNKS
        small_prng rand;
        random_shuffle(chunks.begin(), chunks.end(), rand);
#endif
        auto append_with_insertion_count_override = [&](chunk_list list,
                                                        uint32_t id) {
            append(list, id);
            if (initial_insertion_count_on_pool_creation_ != 0) {
                auto override_insertion_count = [&](detail::db_metadata *db) {
                    auto g = db->hold_dirty();
                    auto *i = db->at_(id);
                    i->insertion_count0_ =
                        uint32_t(initial_insertion_count_on_pool_creation_) &
                        0x3ff;
                    i->insertion_count1_ =
                        uint32_t(
                            initial_insertion_count_on_pool_creation_ >> 10) &
                        0x3ff;
                };
                override_insertion_count(db_metadata_[0]);
                override_insertion_count(db_metadata_[1]);
            }
        };
        // root offset is the front of fast list
        chunk_offset_t const fast_offset(chunks.front(), 0);
        append_with_insertion_count_override(chunk_list::fast, fast_offset.id);
        // init the first slow chunk and slow_offset
        chunk_offset_t const slow_offset(chunks[1], 0);
        append_with_insertion_count_override(chunk_list::slow, slow_offset.id);
        std::span const chunks_after_second(
            chunks.data() + 2, chunks.size() - 2);
        // insert the rest of the chunks to free list
        for (uint32_t const i : chunks_after_second) {
            append(chunk_list::free, i);
        }

        // Mark as done
        advance_offsets_to(fast_offset, fast_offset, slow_offset);
        MONAD_ASSERT(get_root_offset().id == db_metadata()->fast_list.begin);

        std::atomic_signal_fence(
            std::memory_order_seq_cst); // no compiler reordering here
        memcpy(db_metadata_[0]->magic, "MND0", 4);
        memcpy(db_metadata_[1]->magic, "MND0", 4);

        // Default behavior: initialize node writers to start at the start
        // of available slow and fast list respectively. Make sure the
        // initial fast/slow offset points into a block in use as a sanity
        // check
        reset_node_writers();
    }
    else { // resume from an existing db and underlying storage devices
        auto build_insertion_count_to_chunk_id =
            [&](auto &lst, detail::db_metadata::chunk_info_t const *i) {
                for (; i != nullptr; i = i->next(db_metadata())) {
                    lst.emplace_back(i->index(db_metadata()));
                }
            };
        build_insertion_count_to_chunk_id(
            insertion_count_to_chunk_id_[uint8_t(chunk_list::free)],
            db_metadata()->free_list_begin());
        build_insertion_count_to_chunk_id(
            insertion_count_to_chunk_id_[uint8_t(chunk_list::fast)],
            db_metadata()->fast_list_begin());
        build_insertion_count_to_chunk_id(
            insertion_count_to_chunk_id_[uint8_t(chunk_list::slow)],
            db_metadata()->slow_list_begin());
        // Reset/init node writer's offsets, destroy contents after
        // fast_offset.id chunck
        rewind_to_match_offsets();
    }
    // If the pool has changed since we configured the metadata, this will
    // fail
    MONAD_ASSERT(db_metadata_[0]->chunk_info_count == chunk_count);
}
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

void UpdateAux::reset_node_writers()
{
    auto init_node_writer = [&](chunk_offset_t const node_writer_offset)
        -> node_writer_unique_ptr_type {
        auto chunk =
            io->storage_pool().chunk(storage_pool::seq, node_writer_offset.id);
        MONAD_ASSERT(chunk->size() >= node_writer_offset.offset);
        return io ? io->make_connected(
                        write_single_buffer_sender{
                            node_writer_offset,
                            std::min(
                                AsyncIO::WRITE_BUFFER_SIZE,
                                size_t(
                                    chunk->capacity() -
                                    node_writer_offset.offset))},
                        write_operation_io_receiver{})
                  : node_writer_unique_ptr_type{};
    };
    node_writer_fast =
        init_node_writer(db_metadata()->db_offsets.start_of_wip_offset_fast);
    node_writer_slow =
        init_node_writer(db_metadata()->db_offsets.start_of_wip_offset_slow);
}

void UpdateAux::restore_state_history_disk_infos(
    Node &root, std::optional<uint64_t> const opt_max_block_id)
{
    // only restore block histories till `opt_till_block_id.value()` if any or
    // the max block id it has on disk
    MONAD_ASSERT(is_on_disk());
    auto const min_block_id = deserialize_from_big_endian<uint64_t>(
        find_min_key_blocking(*this, root));
    auto const existing_max_block_id = deserialize_from_big_endian<uint64_t>(
        find_max_key_blocking(*this, root));
    uint64_t const max_block_id =
        opt_max_block_id.value_or(existing_max_block_id);
    MONAD_ASSERT(max_block_id <= existing_max_block_id);
    for (auto i = min_block_id; i <= max_block_id; i++) {
        auto [state_root, res] = find_blocking(
            *this, &root, serialize_as_big_endian<BLOCK_NUM_BYTES>(i));
        MONAD_ASSERT(res == find_result::success);
        auto [min_offset_fast, min_offset_slow] = calc_min_offsets(*state_root);
        if (min_offset_fast == INVALID_COMPACT_VIRTUAL_OFFSET) {
            min_offset_fast.set_value(0);
        }
        if (min_offset_slow == INVALID_COMPACT_VIRTUAL_OFFSET) {
            min_offset_slow.set_value(0);
        }
        if (i > min_block_id) {
            state_histories.back().max_offset_fast.set_value(
                unaligned_load<uint32_t>(
                    state_root->value().substr(0, 4).data()));
            state_histories.back().max_offset_slow.set_value(
                unaligned_load<uint32_t>(state_root->value().substr(4).data()));
        }
        state_histories.emplace_back(
            i,
            min_offset_fast,
            min_offset_slow,
            (i < max_block_id)
                ? MIN_COMPACT_VIRTUAL_OFFSET
                : compact_virtual_chunk_offset_t{this->physical_to_virtual(
                      node_writer_fast->sender().offset())},
            (i < max_block_id)
                ? MIN_COMPACT_VIRTUAL_OFFSET
                : compact_virtual_chunk_offset_t{this->physical_to_virtual(
                      node_writer_slow->sender().offset())});
    }
}

/* upsert() supports both on disk and in memory db updates. User should always
use this interface to upsert updates to db. Here are what it does:
- copy state from last version to new version if new version not yet exist;
- erase outdated history block if any; if `compaction`, update compaction
offsets;
- upsert `updates` should include everything nested under
version number;
- if it's on disk, update db_metadata min max versions.

Note that `version` on each call of upsert() does not always grow incrementally,
the call sequence can be 0,1,2,3,4, then restore db from version 3,4,5,6,7...
But if db has version 0,1,2,3,4, and user skips version 5 and the call upsert
version 6 would fail.
*/
Node::UniquePtr UpdateAux::do_update(
    Node::UniquePtr prev_root, StateMachine &sm, UpdateList &&updates,
    uint64_t const version, bool const compaction)
{
    auto const curr_version = serialize_as_big_endian<BLOCK_NUM_BYTES>(version);
    // 1. copy state if version not exists and db is not empty
    if (!contains_version(prev_root, version) &&
        contains_version(prev_root, version - 1)) { // empty db won't enter if
        auto const prev_version =
            serialize_as_big_endian<BLOCK_NUM_BYTES>(version - 1);
        prev_root =
            copy_node(*this, std::move(prev_root), prev_version, curr_version);
    }
    UpdateList db_updates;
    Update u = make_update(
        curr_version, byte_string_view{}, false, std::move(updates));
    db_updates.push_front(u);
    fprintf(stdout, "Insert version_id %lu\n", version);
    // 2. erase any outdated states from history
    uint64_t min_version = prev_root ? min_version_in_db(*prev_root) : version;
    byte_string version_to_erase;
    Update erase;
    if (prev_root &&
        max_version_in_db(*prev_root) - min_version >= version_history_len) {
        version_to_erase =
            serialize_as_big_endian<BLOCK_NUM_BYTES>(min_version);
        erase = make_erase(version_to_erase);
        db_updates.push_front(erase);
        fprintf(stdout, "erase version_id %lu\n", min_version);
        min_version++;
        // set chunk count that can be erased
        auto [erase_root, res] =
            find_blocking(*this, prev_root.get(), version_to_erase);
        auto [min_offset_fast, min_offset_slow] = calc_min_offsets(*erase_root);
        MONAD_ASSERT(min_offset_fast != INVALID_COMPACT_VIRTUAL_OFFSET);
        if (min_offset_slow == INVALID_COMPACT_VIRTUAL_OFFSET) {
            min_offset_slow = MIN_COMPACT_VIRTUAL_OFFSET;
        }
        remove_chunks_before_count_fast_ = min_offset_fast.get_count();
        remove_chunks_before_count_slow_ = min_offset_slow.get_count();
    }

    // 3. advance compaction offsets
    if (compaction) {
        advance_compact_offsets();
    }
    // 4. upsert version updates
    auto root = upsert(*this, sm, std::move(prev_root), std::move(db_updates));
    // 5. free compacted chunks and update version metadata if on disk
    if (is_on_disk()) {
        free_compacted_chunks();
        update_version_metadata(min_version, version);
    }
    return root;
}

Node::UniquePtr UpdateAux::upsert_with_fixed_history_len(
    Node::UniquePtr prev_root, StateMachine &sm, UpdateList &&updates,
    uint64_t const block_id, bool const compaction)
{
    auto block_num = serialize_as_big_endian<BLOCK_NUM_BYTES>(block_id);
    fprintf(stdout, "Insert block_id %lu\n", block_id);
    if (!state_histories.empty()) {
        MONAD_ASSERT(block_id == max_block_id_in_history() + 1);
        // copy old state if any
        auto prev_block_num =
            serialize_as_big_endian<BLOCK_NUM_BYTES>(max_block_id_in_history());
        prev_root =
            copy_node(*this, std::move(prev_root), prev_block_num, block_num);
    }

    UpdateList block_updates;
    // erase any outdated states from history
    byte_string block_to_erase;
    state_disk_info_t erased_state_info;
    Update e;
    if (state_histories.size() > version_history_len) {
        printf("erase block id %lu\n", min_block_id_in_history());
        block_to_erase =
            serialize_as_big_endian<BLOCK_NUM_BYTES>(min_block_id_in_history());
        e = make_erase(block_to_erase);
        block_updates.push_front(e);
        erased_state_info = state_histories.front();
        state_histories.pop_front();
        if (compaction) {
            advance_compact_offsets(erased_state_info);
        }
    }
    // value under block_num is the `concat(min_offset_fast +
    // min_offset_slow)` byte_string
    auto last_block_max_offsets =
        serialize((uint32_t)compact_virtual_chunk_offset_t{
            this->physical_to_virtual(node_writer_fast->sender().offset())}) +
        serialize((uint32_t)compact_virtual_chunk_offset_t{
            this->physical_to_virtual(node_writer_slow->sender().offset())});
    Update u = make_update(
        block_num, last_block_max_offsets, false, std::move(updates));
    block_updates.push_front(u);

    // upsert changes
    auto root =
        upsert(*this, sm, std::move(prev_root), std::move(block_updates));
    if (compaction) {
        this->free_compacted_chunks();
    }
    state_histories.emplace_back(
        block_id,
        compact_offset_fast,
        compact_offset_slow,
        compact_virtual_chunk_offset_t{
            this->physical_to_virtual(node_writer_fast->sender().offset())},
        compact_virtual_chunk_offset_t{
            this->physical_to_virtual(node_writer_slow->sender().offset())});
    return root;
}

void UpdateAux::advance_compact_offsets()
{
    MONAD_ASSERT(is_on_disk());
    // update disk growth speed trackers
    compact_virtual_chunk_offset_t const curr_fast_writer_offset{
        physical_to_virtual(node_writer_fast->sender().offset())};
    compact_virtual_chunk_offset_t const curr_slow_writer_offset{
        physical_to_virtual(node_writer_slow->sender().offset())};
    last_block_disk_growth_fast_ =
        last_block_end_offset_fast_ == 0
            ? MIN_COMPACT_VIRTUAL_OFFSET
            : curr_fast_writer_offset - last_block_end_offset_fast_;
    last_block_disk_growth_slow_ =
        last_block_end_offset_slow_ == 0
            ? MIN_COMPACT_VIRTUAL_OFFSET
            : curr_slow_writer_offset - last_block_end_offset_slow_;
    last_block_end_offset_fast_ = curr_fast_writer_offset;
    last_block_end_offset_slow_ = curr_slow_writer_offset;

    compact_offset_fast = db_metadata()->db_offsets.last_compact_offset_fast;
    compact_offset_slow = db_metadata()->db_offsets.last_compact_offset_slow;

    double const used_chunks_ratio =
        1 - num_chunks(chunk_list::free) / (double)io->chunk_count();
    printf(
        "Disk Usage: free chunks %llu, total chunk %lu, disk used chunks "
        "ratio %.3f. Fastlist has %u chunks, Slowlist has %u chunks\n",
        get_lower_bound_free_space() >> 28,
        io->chunk_count(),
        used_chunks_ratio,
        num_chunks(chunk_list::fast),
        num_chunks(chunk_list::slow));
    compact_offset_range_fast_ = MIN_COMPACT_VIRTUAL_OFFSET;
    compact_offset_range_slow_ = MIN_COMPACT_VIRTUAL_OFFSET;
    // Compaction pace control based on free space left on disk
    if (num_chunks(chunk_list::fast) <= 100 /* disk usage less than 25GB */) {
        printf("NO COMPACT::");
    }
    else if (used_chunks_ratio <= 0.8) {
        printf("SLOW COMPACT::");
        compact_offset_range_fast_.set_value(
            (uint32_t)std::lround(last_block_disk_growth_fast_ * 0.7));
    }
    else {
        auto slow_fast_inuse_ratio =
            (double)num_chunks(chunk_list::slow) / num_chunks(chunk_list::fast);
        if (db_metadata()->slow_fast_ratio == 0.0) {
            update_slow_fast_ratio_metadata();
        }
        if (slow_fast_inuse_ratio < db_metadata()->slow_fast_ratio) {
            printf("FAST COMPACT::");
            // slow can continue to grow
            compact_offset_range_slow_.set_value((uint32_t)std::lround(
                (double)last_block_disk_growth_slow_ *
                ((double)slow_fast_inuse_ratio /
                 db_metadata()->slow_fast_ratio)));
            // more agressive compaction on fast chunks
            compact_offset_range_fast_.set_value(
                last_block_disk_growth_fast_ + last_block_disk_growth_slow_ -
                compact_offset_range_slow_ + 5);
        }
        else {
            printf("FAST COMPACT ALSO ON SLOW RING::");
            // compact slow list more agressively until ratio is met
            compact_offset_range_fast_.set_value(
                (uint32_t)std::lround(last_block_disk_growth_fast_ * 0.99));
            // slow can continue to grow
            compact_offset_range_slow_.set_value(
                std::max(
                    db_metadata()->db_offsets.last_compact_offset_range_slow,
                    last_block_disk_growth_slow_) +
                2);
        }
    }
    compact_offset_fast += compact_offset_range_fast_;
    compact_offset_slow += compact_offset_range_slow_;
    // correcting offsets
    // TEMPORARY
    auto min_fast_offset = MIN_COMPACT_VIRTUAL_OFFSET,
         min_slow_offset = MIN_COMPACT_VIRTUAL_OFFSET;
    if (!state_histories.empty()) {
        // latest block min offsets
        min_fast_offset = state_histories.back().min_offset_fast;
        min_slow_offset = state_histories.back().min_offset_slow;
        if (min_slow_offset > uint32_t(-1) / 2) {
            min_slow_offset.set_value(0);
        }
        if (min_fast_offset > uint32_t(-1) / 2) {
            min_fast_offset.set_value(0);
        }
    }
    compact_offset_fast = std::max(compact_offset_fast, min_fast_offset);
    compact_offset_slow = std::max(compact_offset_slow, min_slow_offset);
    compact_offset_range_fast_ =
        compact_offset_fast -
        db_metadata()->db_offsets.last_compact_offset_fast;
    compact_offset_range_slow_ =
        compact_offset_slow -
        db_metadata()->db_offsets.last_compact_offset_slow;

    printf(
        "  Fast: last block disk grew %u ~%u MB, compact range %u\n"
        "\tSlow: last block disk grew %u ~%u MB, compact range %u\n",
        (uint32_t)last_block_disk_growth_fast_,
        (uint32_t)last_block_disk_growth_fast_ >> 4,
        (uint32_t)compact_offset_range_fast_,
        (uint32_t)last_block_disk_growth_slow_,
        (uint32_t)last_block_disk_growth_slow_ >> 4,
        (uint32_t)compact_offset_range_slow_);
}

// must call this when db is non empty
uint64_t UpdateAux::min_version_in_db(Node &root) const noexcept
{
    if (is_in_memory()) {
        auto const min_version = find_min_key_blocking(*this, root);
        MONAD_ASSERT(min_version.nibble_size() == BLOCK_NUM_NIBBLES_LEN);
        return deserialize_from_big_endian<uint64_t>(min_version);
    }
    else {
        return db_metadata()->min_version;
    }
}

uint64_t UpdateAux::max_version_in_db(Node &root) const noexcept
{
    if (is_in_memory()) {
        auto const max_version = find_max_key_blocking(*this, root);
        MONAD_ASSERT(max_version.nibble_size() == BLOCK_NUM_NIBBLES_LEN);
        return deserialize_from_big_endian<uint64_t>(max_version);
    }
    else {
        return db_metadata()->max_version;
    }
}

bool UpdateAux::contains_version(
    Node::UniquePtr &root, uint64_t const version) const noexcept
{
    return root && version >= min_version_in_db(*root) &&
           version <= max_version_in_db(*root);
}

void UpdateAux::advance_compact_offsets(state_disk_info_t erased_state_info)
{
    MONAD_ASSERT(is_on_disk());

    // update disk growth speed trackers
    compact_virtual_chunk_offset_t const curr_fast_writer_offset{
        physical_to_virtual(node_writer_fast->sender().offset())};
    compact_virtual_chunk_offset_t const curr_slow_writer_offset{
        physical_to_virtual(node_writer_slow->sender().offset())};
    last_block_disk_growth_fast_ =
        last_block_end_offset_fast_ == 0
            ? MIN_COMPACT_VIRTUAL_OFFSET
            : curr_fast_writer_offset - last_block_end_offset_fast_;
    last_block_disk_growth_slow_ =
        last_block_end_offset_slow_ == 0
            ? MIN_COMPACT_VIRTUAL_OFFSET
            : curr_slow_writer_offset - last_block_end_offset_slow_;
    last_block_end_offset_fast_ = curr_fast_writer_offset;
    last_block_end_offset_slow_ = curr_slow_writer_offset;

    // update compaction variables
    remove_chunks_before_count_fast_ =
        erased_state_info.min_offset_fast.get_count();
    remove_chunks_before_count_slow_ =
        erased_state_info.min_offset_slow.get_count();

    compact_offset_fast = db_metadata()->db_offsets.last_compact_offset_fast;
    compact_offset_slow = db_metadata()->db_offsets.last_compact_offset_slow;

    double const used_chunks_ratio =
        1 - num_chunks(chunk_list::free) / (double)io->chunk_count();
    printf(
        "Disk Usage: free chunks %llu, total chunk %lu, disk used chunks "
        "ratio %.3f. Fastlist has %u chunks, Slowlist has %u chunks\n",
        get_lower_bound_free_space() >> 28,
        io->chunk_count(),
        used_chunks_ratio,
        num_chunks(chunk_list::fast),
        num_chunks(chunk_list::slow));
    compact_offset_range_fast_ = MIN_COMPACT_VIRTUAL_OFFSET;
    compact_offset_range_slow_ = MIN_COMPACT_VIRTUAL_OFFSET;
    // Compaction pace control based on free space left on disk
    if (used_chunks_ratio <= 0.2 && state_histories.front().block_id == 0) {
        printf("NO COMPACT::");
    }
    else if (used_chunks_ratio <= 0.8) {
        printf("SLOW COMPACT::");
        compact_offset_range_fast_ = std::min(
            erased_state_info.max_offset_fast - compact_offset_fast,
            last_block_disk_growth_fast_);
    }
    else {
        auto slow_fast_inuse_ratio =
            (double)num_chunks(chunk_list::slow) / num_chunks(chunk_list::fast);
        if (db_metadata()->slow_fast_ratio == 0.0) {
            update_slow_fast_ratio_metadata();
        }
        if (slow_fast_inuse_ratio < db_metadata()->slow_fast_ratio) {
            printf("FAST COMPACT::");
            // slow can continue to grow
            compact_offset_range_slow_.set_value((uint32_t)std::lround(
                (double)last_block_disk_growth_slow_ *
                ((double)slow_fast_inuse_ratio /
                 db_metadata()->slow_fast_ratio)));
            // more agressive compaction on fast chunks
            compact_offset_range_fast_.set_value(
                last_block_disk_growth_fast_ + last_block_disk_growth_slow_ -
                compact_offset_range_slow_ + 5);
        }
        else {
            printf("FAST COMPACT ALSO ON SLOW RING::");
            // compact slow list more agressively until ratio is met
            compact_offset_range_fast_.set_value(
                (uint32_t)std::lround(last_block_disk_growth_fast_ * 0.99));
            // slow can continue to grow
            compact_offset_range_slow_.set_value(
                std::max(
                    db_metadata()->db_offsets.last_compact_offset_range_slow,
                    last_block_disk_growth_slow_) +
                2);
        }
    }
    compact_offset_fast += compact_offset_range_fast_;
    compact_offset_slow += compact_offset_range_slow_;
    // correcting offsets
    // TEMPORARY
    auto min_fast_offset = MIN_COMPACT_VIRTUAL_OFFSET,
         min_slow_offset = MIN_COMPACT_VIRTUAL_OFFSET;
    if (!state_histories.empty()) {
        // latest block min offsets
        min_fast_offset = state_histories.back().min_offset_fast;
        min_slow_offset = state_histories.back().min_offset_slow;
        if (min_slow_offset > uint32_t(-1) / 2) {
            min_slow_offset.set_value(0);
        }
        if (min_fast_offset > uint32_t(-1) / 2) {
            min_fast_offset.set_value(0);
        }
    }
    compact_offset_fast = std::max(compact_offset_fast, min_fast_offset);
    compact_offset_slow = std::max(compact_offset_slow, min_slow_offset);
    compact_offset_range_fast_ =
        compact_offset_fast -
        db_metadata()->db_offsets.last_compact_offset_fast;
    compact_offset_range_slow_ =
        compact_offset_slow -
        db_metadata()->db_offsets.last_compact_offset_slow;

    printf(
        "  Fast: last block disk grew %u ~%u MB, compact range %u\n"
        "\tSlow: last block disk grew %u ~%u MB, compact range %u\n",
        (uint32_t)last_block_disk_growth_fast_,
        (uint32_t)last_block_disk_growth_fast_ >> 4,
        (uint32_t)compact_offset_range_fast_,
        (uint32_t)last_block_disk_growth_slow_,
        (uint32_t)last_block_disk_growth_slow_ >> 4,
        (uint32_t)compact_offset_range_slow_);
}

void UpdateAux::free_compacted_chunks()
{
    auto free_chunks_from_ci_till_count =
        [&](detail::db_metadata::chunk_info_t const *ci,
            uint32_t const count_before) {
            uint32_t idx = ci->index(db_metadata()),
                     count =
                         (uint32_t)db_metadata()->at(idx)->insertion_count();
            printf("begin id %u count %u, ", idx, count);
            for (; count < count_before && ci != nullptr;
                 idx = ci->index(db_metadata()),
                 count = (uint32_t)db_metadata()->at(idx)->insertion_count()) {
                ci = ci->next(db_metadata()); // must be in this order
                remove(idx);
                io->storage_pool()
                    .chunk(monad::async::storage_pool::seq, idx)
                    ->destroy_contents();
                append(UpdateAux::chunk_list::free, idx); // append not prepend
                printf("free id %u count %u, ", idx, count);
            }
        };
    MONAD_ASSERT(
        remove_chunks_before_count_fast_ >=
            db_metadata()->fast_list_begin()->insertion_count() &&
        remove_chunks_before_count_fast_ <=
            db_metadata()->fast_list_end()->insertion_count());
    MONAD_ASSERT(
        remove_chunks_before_count_slow_ >=
            db_metadata()->slow_list_begin()->insertion_count() &&
        remove_chunks_before_count_slow_ <=
            db_metadata()->slow_list_end()->insertion_count());
    printf("Fast Chunks: ");
    free_chunks_from_ci_till_count(
        db_metadata()->fast_list_begin(), remove_chunks_before_count_fast_);
    printf("\nSlow Chunk: ");
    free_chunks_from_ci_till_count(
        db_metadata()->slow_list_begin(), remove_chunks_before_count_slow_);
    printf("\n");
}

uint32_t UpdateAux::num_chunks(chunk_list const list) const noexcept
{
    switch (list) {
    case chunk_list::free:
        return (
            uint32_t)(db_metadata_[0]->free_list_end()->insertion_count() -
                      db_metadata_[0]->free_list_begin()->insertion_count());
    case chunk_list::fast:
        return (
            uint32_t)(db_metadata_[0]->fast_list_end()->insertion_count() -
                      db_metadata_[0]->fast_list_begin()->insertion_count());
    case chunk_list::slow:
        return (
            uint32_t)(db_metadata_[0]->slow_list_end()->insertion_count() -
                      db_metadata_[0]->slow_list_begin()->insertion_count());
    }
    return 0;
}

void UpdateAux::print_update_stats()
{
#if MONAD_MPT_COLLECT_STATS
    printf("created/updated nodes: %u\n", stats.num_nodes_created);

    if (compact_offset_fast || compact_offset_slow) {
        printf(
            "#nodes copied fast to slow ring %u (%.4f), fast to fast %u "
            "(%.4f), slow to slow %u, total #nodes copied %u\n"
            "#nodes copied for compacting fast %u, #nodes copied for "
            "compacting slow %u\n",
            stats.nodes_copied_from_fast_to_slow,
            (double)stats.nodes_copied_from_fast_to_slow /
                (stats.nodes_copied_from_fast_to_slow +
                 stats.nodes_copied_from_fast_to_fast),
            stats.nodes_copied_from_fast_to_fast,
            (double)stats.nodes_copied_from_fast_to_fast /
                (stats.nodes_copied_from_fast_to_slow +
                 stats.nodes_copied_from_fast_to_fast),
            stats.nodes_copied_from_slow_to_slow,
            stats.nodes_copied_from_fast_to_slow +
                stats.nodes_copied_from_fast_to_fast +
                stats.nodes_copied_from_slow_to_slow,
            stats.nodes_copied_for_compacting_fast,
            stats.nodes_copied_for_compacting_slow);
        if (compact_offset_fast) {
            printf(
                "Fast: #compact reads before compaction offset %u / "
                "#total compact reads %u = %.4f\n",
                stats.nreads_before_offset[0],
                stats.nreads_before_offset[0] + stats.nreads_after_offset[0],
                (double)stats.nreads_before_offset[0] /
                    (stats.nreads_before_offset[0] +
                     stats.nreads_after_offset[0]));
            if (compact_offset_range_fast_) {
                printf(
                    "Fast: bytes read within compaction range %.2f MB / "
                    "compaction offset range %.2f MB = %.4f\n",
                    (double)stats.bytes_read_before_offset[0] / 1024 / 1024,
                    (double)compact_offset_range_fast_ / 16,
                    (double)stats.bytes_read_before_offset[0] /
                        compact_offset_range_fast_ / 1024 / 64);
            }
        }
        if (compact_offset_slow != 0) {
            printf(
                "Slow: #compact reads before compaction offset %u / "
                "#total compact reads %u = %.4f\n",
                stats.nreads_before_offset[1],
                stats.nreads_before_offset[1] + stats.nreads_after_offset[1],
                (double)stats.nreads_before_offset[1] /
                    (stats.nreads_before_offset[1] +
                     stats.nreads_after_offset[1]));
            if (compact_offset_range_slow_) {
                printf(
                    "Slow: bytes read within compaction range %.2f MB / "
                    "compaction offset range %.2f MB = %.4f\n",
                    (double)stats.bytes_read_before_offset[1] / 1024 / 1024,
                    (double)compact_offset_range_slow_ / 16,
                    (double)stats.bytes_read_before_offset[1] /
                        compact_offset_range_slow_ / 1024 / 64);
            }
        }
    }
#endif
}

void UpdateAux::reset_stats()
{
#if MONAD_MPT_COLLECT_STATS
    stats.reset();
#endif
}

void UpdateAux::collect_number_nodes_created_stats()
{
#if MONAD_MPT_COLLECT_STATS
    stats.num_nodes_created++;
#endif
}

void UpdateAux::collect_compaction_read_stats(
    virtual_chunk_offset_t const node_offset, unsigned const bytes_to_read)
{
#if MONAD_MPT_COLLECT_STATS
    if (compact_virtual_chunk_offset_t(node_offset) <
        (node_offset.in_fast_list() ? compact_offset_fast
                                    : compact_offset_slow)) {
        // node orig offset in fast list but compact to slow list
        stats.nreads_before_offset[!node_offset.in_fast_list()]++;
        stats.bytes_read_before_offset[!node_offset.in_fast_list()] +=
            bytes_to_read; // compaction bytes read
    }
    else {
        stats.nreads_after_offset[!node_offset.in_fast_list()]++;
        stats.bytes_read_before_offset[!node_offset.in_fast_list()] +=
            bytes_to_read;
    }
    stats.num_compaction_reads++; // count number of compaction reads
#else
    (void)node_offset;
    (void)bytes_to_read;
#endif
}

void UpdateAux::collect_compacted_nodes_stats(
    compact_virtual_chunk_offset_t const subtrie_min_offset_fast,
    compact_virtual_chunk_offset_t const subtrie_min_offset_slow)
{
#if MONAD_MPT_COLLECT_STATS
    if (subtrie_min_offset_fast < compact_offset_fast) {
        stats.nodes_copied_for_compacting_fast++;
    }
    else if (subtrie_min_offset_slow < compact_offset_slow) {
        stats.nodes_copied_for_compacting_slow++;
    }
#else
    (void)subtrie_min_offset_fast;
    (void)subtrie_min_offset_slow;
#endif
}

void UpdateAux::collect_compacted_nodes_from_to_stats(
    virtual_chunk_offset_t const node_offset, bool const rewrite_to_fast)
{
#if MONAD_MPT_COLLECT_STATS
    if (node_offset != INVALID_VIRTUAL_OFFSET) {
        if (node_offset.in_fast_list()) {
            if (!rewrite_to_fast) {
                stats.nodes_copied_from_fast_to_slow++;
            }
            else {
                stats.nodes_copied_from_fast_to_fast++;
            }
        }
        else {
            stats.nodes_copied_from_slow_to_slow++;
        }
    }
#else
    (void)node_offset;
    (void)rewrite_to_fast;

#endif
}

MONAD_MPT_NAMESPACE_END
