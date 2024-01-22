#include <monad/async/config.hpp>
#include <monad/core/assert.h>
#include <monad/core/small_prng.hpp>
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
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

// Define to avoid randomisation of free list chunks on pool creation
// This can be useful to discover bugs in code which assume chunks are
// consecutive
// #define MONAD_MPT_INITIALIZE_POOL_WITH_CONSECUTIVE_CHUNKS 1

uint32_t UpdateAux::chunk_id_from_insertion_count(
    chunk_list list, detail::unsigned_20 insertion_count) const noexcept
{
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

chunk_offset_t
UpdateAux::physical_to_virtual(chunk_offset_t offset) const noexcept
{
    MONAD_DEBUG_ASSERT(offset.get_highest_bit() == false);
    auto const *ci = db_metadata_[0]->at(offset.id);
    // free list offset never enter this function
    MONAD_DEBUG_ASSERT(ci->in_fast_list || ci->in_slow_list);
    // Use top bit in id for slow or fast list
    offset.id = uint32_t(ci->insertion_count()) & chunk_offset_t::max_id;
    offset.set_highest_bit(ci->in_fast_list);
    return offset;
}

chunk_offset_t
UpdateAux::virtual_to_physical(chunk_offset_t offset) const noexcept
{
    MONAD_DEBUG_ASSERT(offset.spare != 0xffff); // not max
    // get top bit in spare and then clear it
    bool const in_fast = offset.get_highest_bit();
    offset.set_highest_bit(false);
    offset.id = chunk_id_from_insertion_count(
                    in_fast ? chunk_list::fast : chunk_list::slow, offset.id) &
                chunk_offset_t::max_id;
    return offset;
}

std::pair<UpdateAux::chunk_list, detail::unsigned_20>
UpdateAux::chunk_list_and_age(uint32_t idx) const noexcept
{
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

void UpdateAux::rewind_to_match_offsets()
{
    // Free all chunks after fast_offset.id
    auto const fast_offset = db_metadata_[0]->start_of_wip_fast_offset;
    auto const *ci = db_metadata_[0]->at(fast_offset.id);
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
    auto const slow_offset = db_metadata_[0]->start_of_wip_slow_offset;
    auto const *slow_ci = db_metadata_[0]->at(slow_offset.id);
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

        // Default behavior: initialize node writers to start at the start of
        // available slow and fast list respectively. Make sure the initial
        // fast/slow offset points into a block in use as a sanity check
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
    // If the pool has changed since we configured the metadata, this will fail
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
        init_node_writer(db_metadata_[0]->start_of_wip_fast_offset);
    node_writer_slow =
        init_node_writer(db_metadata_[0]->start_of_wip_slow_offset);
}

MONAD_MPT_NAMESPACE_END
