#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/detail/start_lifetime_as_polyfill.hpp>
#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/io_senders.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/core/small_prng.hpp>
#include <monad/mpt/cache_option.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/detail/unsigned_20.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>
#include <monad/mpt/upward_tnode.hpp>
#include <monad/mpt/util.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <sys/mman.h>
#include <unistd.h>

// Define to avoid randomisation of free list chunks on pool creation
// This can be useful to discover bugs in code which assume chunks are
// consecutive
// #define MONAD_MPT_INITIALIZE_POOL_WITH_CONSECUTIVE_CHUNKS 1

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

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
    if (list == chunk_list::free) {
        auto chunk = io->storage_pool().chunk(
            MONAD_ASYNC_NAMESPACE::storage_pool::seq, idx);
        auto capacity = chunk->capacity();
        assert(chunk->size() == 0);
        db_metadata_[0]->free_capacity_add_(capacity);
        db_metadata_[1]->free_capacity_add_(capacity);
    }
}

void UpdateAux::prepend(chunk_list list, uint32_t idx) noexcept
{
    auto do_ = [&](detail::db_metadata *m) {
        switch (list) {
        case chunk_list::free:
            m->prepend_(m->free_list, m->at_(idx));
            break;
        case chunk_list::fast:
            m->prepend_(m->fast_list, m->at_(idx));
            break;
        case chunk_list::slow:
            m->prepend_(m->slow_list, m->at_(idx));
            break;
        }
    };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
    if (list == chunk_list::free) {
        auto chunk = io->storage_pool().chunk(
            MONAD_ASYNC_NAMESPACE::storage_pool::seq, idx);
        auto capacity = chunk->capacity();
        assert(chunk->size() == 0);
        db_metadata_[0]->free_capacity_add_(capacity);
        db_metadata_[1]->free_capacity_add_(capacity);
    }
}

void UpdateAux::remove(uint32_t idx) noexcept
{
    bool const is_free_list =
        (!db_metadata_[0]->at_(idx)->in_fast_list &&
         !db_metadata_[0]->at_(idx)->in_slow_list);
    auto do_ = [&](detail::db_metadata *m) { m->remove_(m->at_(idx)); };
    do_(db_metadata_[0]);
    do_(db_metadata_[1]);
    if (is_free_list) {
        auto chunk = io->storage_pool().chunk(
            MONAD_ASYNC_NAMESPACE::storage_pool::seq, idx);
        auto capacity = chunk->capacity();
        assert(chunk->size() == 0);
        db_metadata_[0]->free_capacity_sub_(capacity);
        db_metadata_[1]->free_capacity_sub_(capacity);
    }
}

void UpdateAux::rewind_offset_to(chunk_offset_t const fast_offset)
{
    /* TODO FIXME: We need to also adjust the slow list, and slow_node_writer's
     * offset */
    // Free all chunks after fast_offset.id
    auto *ci = db_metadata_[0]->at(fast_offset.id);
    while (ci != db_metadata_[0]->fast_list_end()) {
        auto const idx = db_metadata_[0]->fast_list.end;
        remove(idx);
        io->storage_pool().chunk(storage_pool::seq, idx)->destroy_contents();
        prepend(chunk_list::free, idx);
    }
    auto fast_offset_chunk =
        io->storage_pool().chunk(storage_pool::seq, fast_offset.id);
    MONAD_ASSERT(fast_offset_chunk->try_trim_contents(fast_offset.offset));

    // Reset node_writer's offset, and buffer too
    node_writer->sender().reset(
        fast_offset,
        {node_writer->sender().buffer().data(),
         std::min(
             AsyncIO::WRITE_BUFFER_SIZE,
             size_t(fast_offset_chunk->capacity() - fast_offset.offset))});
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
void UpdateAux::set_io(MONAD_ASYNC_NAMESPACE::AsyncIO *io_)
{
    io = io_;
    auto const chunk_count = io->chunk_count();
    auto const map_size =
        sizeof(detail::db_metadata) +
        chunk_count * sizeof(detail::db_metadata::chunk_info_t);
    auto cnv_chunk = io->storage_pool().activate_chunk(storage_pool::cnv, 0);
    auto fd = cnv_chunk->write_fd(0);
    db_metadata_[0] = start_lifetime_as<detail::db_metadata>(::mmap(
        nullptr,
        map_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd.first,
        off_t(fd.second)));
    MONAD_ASSERT(db_metadata_[0] != MAP_FAILED);
    db_metadata_[1] = start_lifetime_as<detail::db_metadata>(::mmap(
        nullptr,
        map_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
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
        assert((chunk_count & ~0xfffffU) == 0);
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
        chunk_offset_t const root_offset(chunks.front(), 0);
        append(chunk_list::fast, root_offset.id);
        std::span const chunks_after_first(
            chunks.data() + 1, chunks.size() - 1);
        for (uint32_t const i : chunks_after_first) {
            append(chunk_list::free, i);
        }

        // Mark as done
        db_metadata_[0]->root_offset = root_offset;
        db_metadata_[1]->root_offset = root_offset;
        std::atomic_signal_fence(
            std::memory_order_seq_cst); // no compiler reordering here
        memcpy(db_metadata_[0]->magic, "MND0", 4);
        memcpy(db_metadata_[1]->magic, "MND0", 4);
    }
    // If the pool has changed since we configured the metadata, this will fail
    MONAD_ASSERT(db_metadata_[0]->chunk_info_count == chunk_count);
    // Default behavior: initialize the node writer to start at the front of
    // fast list, and it can be reset later in `rewind_offset_to()`.
    // Make sure the initial fast offset points into a block in use as
    // a sanity check
    chunk_offset_t const default_offset_to_start{
        db_metadata_[0]->fast_list.begin, 0};
    auto chunk =
        io->storage_pool().chunk(storage_pool::seq, default_offset_to_start.id);
    MONAD_ASSERT(chunk->size() >= default_offset_to_start.offset);
    chunk_offset_t const node_writer_offset(default_offset_to_start);
    node_writer =
        io ? io->make_connected(
                 MONAD_ASYNC_NAMESPACE::write_single_buffer_sender{
                     node_writer_offset,
                     {(std::byte const *)nullptr,
                      std::min(
                          MONAD_ASYNC_NAMESPACE::AsyncIO::WRITE_BUFFER_SIZE,
                          size_t(
                              chunk->capacity() - node_writer_offset.offset))}},
                 write_operation_io_receiver{})
           : node_writer_unique_ptr_type{};
}
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif

// invoke at the end of each block upsert
async_write_node_result
write_new_root_node(UpdateAux &aux, tnode_unique_ptr &root_tnode);

/* Names: `prefix_index` is nibble index in prefix of an update,
 `old_prefix_index` is nibble index of relpath in previous node - old.
 `*_prefix_index_start` is the starting nibble index in current function frame
*/
bool dispatch_updates_flat_list_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned prefix_index);

bool dispatch_updates_impl_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned prefix_index,
    std::optional<byte_string_view> opt_leaf_data);

bool mismatch_handler_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned const old_prefix_index,
    unsigned const prefix_index);

Node *create_new_trie_(
    UpdateAux &aux, TrieStateMachine &sm, UpdateList &&updates,
    unsigned prefix_index = 0);

Node *create_new_trie_from_requests_(
    UpdateAux &aux, TrieStateMachine &sm, Requests &requests,
    NibblesView const relpath, unsigned const prefix_index,
    std::optional<byte_string_view> const opt_leaf_data);

bool upsert_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, chunk_offset_t const old_offset,
    UpdateList &&updates, unsigned prefix_index = 0,
    unsigned old_prefix_index = 0);

bool create_node_from_children_if_any_possibly_ondisk(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode *tnode,
    unsigned const prefix_index);

node_ptr upsert(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old, UpdateList &&updates)
{
    sm.reset();
    auto root_tnode = make_tnode(sm.get_state());
    if (!old) {
        root_tnode->node = create_new_trie_(aux, sm, std::move(updates));
    }
    else {
        if (!upsert_(
                aux,
                sm,
                old,
                root_tnode.get(),
                INVALID_OFFSET,
                std::move(updates))) {
            assert(aux.is_on_disk());
            aux.io->flush();
            MONAD_ASSERT(root_tnode->npending == 0);
        }
        if (!root_tnode->node) {
            return {};
        }
    }
    if (aux.is_on_disk()) {
        write_new_root_node(aux, root_tnode);
    }
    return node_ptr{root_tnode->node};
}

void upward_update(UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode *tnode)
{
    sm.reset(tnode->trie_section);
    bool beginning = true;
    while (!tnode->npending) {
        if (beginning) {
            beginning = false;
        }
        else {
            MONAD_DEBUG_ASSERT(tnode->children.size()); // not a leaf
            if (!create_node_from_children_if_any_possibly_ondisk(
                    aux, sm, tnode, tnode->prefix_index)) {
                // create_node not finished, but issued an async read
                return;
            }
        }
        if (!tnode->parent) {
            MONAD_ASSERT(tnode->branch == INVALID_BRANCH);
            return;
        }
        auto *parent_tnode = tnode->parent;
        sm.reset(parent_tnode->trie_section);
        if (tnode->node) {
            auto &entry = parent_tnode->children[tnode->child_index()];
            entry.branch = tnode->branch;
            entry.ptr = tnode->node;
            auto const len = sm.get_compute().compute(entry.data, entry.ptr);
            MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
            entry.len = static_cast<uint8_t>(len);
        }
        else { // node ends up being removed by erase updates
            parent_tnode->mask &= static_cast<uint16_t>(~(1u << tnode->branch));
        }
        --parent_tnode->npending;
        UpwardTreeNode *p = parent_tnode;
        {
            tnode_unique_ptr const _{tnode};
        }
        tnode = p;
    }
}

struct update_receiver
{
    UpdateAux *aux;
    chunk_offset_t rd_offset;
    UpdateList updates;
    UpwardTreeNode *tnode;
    uint16_t buffer_off;
    unsigned bytes_to_read;
    std::unique_ptr<TrieStateMachine> sm;

    update_receiver(
        UpdateAux *aux_, std::unique_ptr<TrieStateMachine> sm_,
        chunk_offset_t offset, UpdateList &&updates_, UpwardTreeNode *tnode_)
        : aux(aux_)
        , rd_offset(round_down_align<DISK_PAGE_BITS>(offset))
        , updates(std::move(updates_))
        , tnode(tnode_)
        , sm(std::move(sm_))
    {
        // prep uring data
        rd_offset.spare = 0;
        buffer_off = uint16_t(offset.offset - rd_offset.offset);
        auto const num_pages_to_load_node =
            offset.spare; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read =
            static_cast<unsigned>(num_pages_to_load_node << DISK_PAGE_BITS);
    }

    void set_value(
        erased_connected_operation *,
        result<std::span<std::byte const>> buffer_)
    {
        MONAD_ASSERT(buffer_);
        std::span<std::byte const> const buffer =
            std::move(buffer_).assume_value();
        // tnode owns the deserialized old node
        node_ptr old = deserialize_node_from_buffer(
            (unsigned char *)buffer.data() + buffer_off);
        Node *old_node = old.get();
        sm->reset(tnode->trie_section);
        if (!upsert_(
                *aux,
                *sm,
                old_node,
                tnode,
                INVALID_OFFSET,
                std::move(updates),
                tnode->prefix_index,
                old_node->bitpacked.path_nibble_index_start)) {
            if (tnode->opt_leaf_data.has_value()) {
                tnode->old = std::move(old);
            }
            return;
        }
        assert(tnode->npending == 0);
        upward_update(*aux, *sm, tnode);
    }
};
static_assert(sizeof(update_receiver) == 56);
static_assert(alignof(update_receiver) == 8);

struct read_single_child_receiver
{
    UpdateAux *aux;
    chunk_offset_t rd_offset;
    UpwardTreeNode *tnode;
    unsigned bytes_to_read;
    uint16_t buffer_off;
    uint8_t j;
    std::unique_ptr<TrieStateMachine> sm;

    read_single_child_receiver(
        UpdateAux *const aux_, std::unique_ptr<TrieStateMachine> sm_,
        UpwardTreeNode *const tnode_, uint8_t const j_)
        : aux(aux_)
        , rd_offset(0, 0)
        , tnode(tnode_)
        , j(j_)
        , sm(std::move(sm_))
    {
        // prep uring data
        auto offset = tnode->children[j].offset;
        rd_offset = round_down_align<DISK_PAGE_BITS>(offset);
        rd_offset.spare = 0;
        buffer_off = uint16_t(offset.offset - rd_offset.offset);
        auto const num_pages_to_load_node =
            offset.spare; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read =
            static_cast<unsigned>(num_pages_to_load_node << DISK_PAGE_BITS);
    }

    void set_value(
        erased_connected_operation *,
        result<std::span<std::byte const>> buffer_)
    {
        MONAD_ASSERT(buffer_);
        std::span<std::byte const> const buffer =
            std::move(buffer_).assume_value();
        // load node from read buffer
        tnode->children[j].ptr =
            deserialize_node_from_buffer(
                (unsigned char *)buffer.data() + buffer_off)
                .release();
        MONAD_ASSERT(create_node_from_children_if_any_possibly_ondisk(
            *aux, *sm, tnode, tnode->prefix_index));
        upward_update(*aux, *sm, tnode);
    }
};
static_assert(sizeof(read_single_child_receiver) == 40);
static_assert(alignof(read_single_child_receiver) == 8);

template <receiver Receiver>
void async_read(UpdateAux &aux, Receiver &&receiver)
{
    read_update_sender sender(receiver);
    auto iostate =
        aux.io->make_connected(std::move(sender), std::move(receiver));
    iostate->initiate();
    // TEMPORARY UNTIL ALL THIS GETS BROKEN OUT: Release
    // management until i/o completes
    iostate.release();
}

// helpers
Node *create_node_from_children_if_any(
    UpdateAux &aux, TrieStateMachine &sm, uint16_t const orig_mask,
    uint16_t const mask, std::span<ChildData> children,
    unsigned const prefix_index, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data = std::nullopt)
{
    // handle non child and single child cases
    auto const number_of_children = static_cast<unsigned>(std::popcount(mask));
    if (number_of_children == 0) {
        return leaf_data.has_value() ? create_leaf(leaf_data.value(), relpath)
                                     : nullptr;
    }
    else if (number_of_children == 1 && !leaf_data.has_value()) {
        auto const j = bitmask_index(
            orig_mask, static_cast<unsigned>(std::countr_zero(mask)));
        assert(children[j].ptr);
        return create_coalesced_node_with_prefix(
            children[j].branch, node_ptr{children[j].ptr}, relpath);
    }
    MONAD_DEBUG_ASSERT(
        number_of_children > 1 ||
        (number_of_children == 1 && leaf_data.has_value()));
    // write children to disk, free any if exceeds the cache level limit
    if (aux.is_on_disk()) {
        for (auto &child : children) {
            if (child.branch != INVALID_BRANCH &&
                child.offset == INVALID_OFFSET) {
                // won't duplicate write of unchanged old child
                MONAD_ASSERT(child.ptr);
                MONAD_ASSERT(child.min_count == uint32_t(-1));
                child.offset =
                    async_write_node(aux, child.ptr).offset_written_to;
                auto const pages =
                    num_pages(child.offset.offset, child.ptr->get_disk_size());
                MONAD_DEBUG_ASSERT(
                    pages <= std::numeric_limits<uint16_t>::max());
                child.offset.spare = static_cast<uint16_t>(pages);
                child.min_count = calc_min_count(
                    child.ptr,
                    aux.db_metadata()->at(child.offset.id)->insertion_count());
                // free node if path longer than CACHE_LEVEL
                // do not free if n == 1, that's when parent is a leaf node with
                // branches
                auto cache_opt = sm.cache_option();
                if (number_of_children > 1 && prefix_index > 0 &&
                    (cache_opt == CacheOption::DisposeAll ||
                     (cache_opt == CacheOption::ApplyLevelBasedCache &&
                      (prefix_index + 1 + child.ptr->path_nibbles_len() >
                       CACHE_LEVEL)))) {
                    {
                        node_ptr const _{child.ptr};
                    }
                    child.ptr = nullptr;
                }
            }
        }
    }
    return create_node(sm.get_compute(), mask, children, relpath, leaf_data);
}

bool create_node_from_children_if_any_possibly_ondisk(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode *tnode,
    unsigned const prefix_index)
{
    if (tnode->number_of_children() == 1) {
        auto const index = bitmask_index(
            tnode->orig_mask,
            static_cast<unsigned>(std::countr_zero(tnode->mask)));
        if (!tnode->children[index].ptr) {
            MONAD_DEBUG_ASSERT(
                prefix_index <= std::numeric_limits<uint8_t>::max());
            tnode->prefix_index = static_cast<uint8_t>(prefix_index);
            read_single_child_receiver receiver(
                &aux, sm.clone(), tnode, static_cast<uint8_t>(index));
            async_read(aux, std::move(receiver));
            return false;
        }
    }
    tnode->node = create_node_from_children_if_any(
        aux,
        sm,
        tnode->orig_mask,
        tnode->mask,
        tnode->children,
        prefix_index,
        tnode->relpath,
        tnode->opt_leaf_data);
    return true;
}

// update leaf data of old, old can have branches
bool update_leaf_data_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Update &update)
{
    if (update.is_deletion()) {
        tnode->node = nullptr;
        return true;
    }
    if (!update.next.empty()) {
        sm.forward();
        tnode->trie_section = sm.get_state();
        Requests requests;
        requests.split_into_sublists(std::move(update.next), 0);
        MONAD_ASSERT(requests.opt_leaf == std::nullopt);
        bool finished = true;
        if (update.incarnation) {
            tnode->node = create_new_trie_from_requests_(
                aux, sm, requests, tnode->relpath, 0, update.value);
        }
        else {
            auto const opt_leaf_data =
                update.value.has_value() ? update.value : old->opt_value();
            finished = dispatch_updates_impl_(
                aux, sm, old, tnode, requests, 0, opt_leaf_data);
        }
        sm.backward();
        return finished;
    }
    // only value update but not subtrie updates
    MONAD_ASSERT(update.value.has_value());
    tnode->node =
        update.incarnation
            ? create_leaf(update.value.value(), tnode->relpath)
            : update_node_diff_path_leaf(old, tnode->relpath, update.value);
    return true;
}

// create a new trie from a list of updates, won't have incarnation
Node *create_new_trie_(
    UpdateAux &aux, TrieStateMachine &sm, UpdateList &&updates,
    unsigned prefix_index)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &update = updates.front();
        MONAD_DEBUG_ASSERT(update.value.has_value());
        auto const relpath = update.key.substr(prefix_index);
        if (!update.next.empty()) {
            sm.forward();
            Requests requests;
            requests.split_into_sublists(std::move(update.next), 0);
            MONAD_DEBUG_ASSERT(update.value.has_value());
            auto ret = create_new_trie_from_requests_(
                aux, sm, requests, relpath, 0, update.value);
            sm.backward();
            return ret;
        }
        return create_leaf(update.value.value(), relpath);
    }
    Requests requests;
    auto const psi = prefix_index;
    while (requests.split_into_sublists(
               std::move(updates), prefix_index) == // NOLINT
               1 &&
           !requests.opt_leaf) {
        updates = std::move(requests).first_and_only_list();
        ++prefix_index;
    }
    return create_new_trie_from_requests_(
        aux,
        sm,
        requests,
        requests.get_first_path().substr(psi, prefix_index - psi),
        prefix_index,
        requests.opt_leaf.and_then(&Update::value));
}

Node *create_new_trie_from_requests_(
    UpdateAux &aux, TrieStateMachine &sm, Requests &requests,
    NibblesView const relpath, unsigned const prefix_index,
    std::optional<byte_string_view> const opt_leaf_data)
{
    auto const number_of_children =
        static_cast<unsigned>(std::popcount(requests.mask));
    uint16_t const mask = requests.mask;
    std::vector<ChildData> children(number_of_children);
    for (unsigned i = 0, j = 0, bit = 1; j < number_of_children;
         ++i, bit <<= 1) {
        if (bit & requests.mask) { // NOLINT
            auto node = create_new_trie_(
                aux, sm, std::move(requests)[i], prefix_index + 1);
            auto &entry = children[j++];
            entry.branch = static_cast<uint8_t>(i);
            entry.ptr = node;
            auto const len = sm.get_compute().compute(entry.data, entry.ptr);
            MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
            entry.len = static_cast<uint8_t>(len);
        }
    }
    return create_node_from_children_if_any(
        aux,
        sm,
        mask,
        mask,
        std::span{children},
        prefix_index,
        relpath,
        opt_leaf_data);
}

bool upsert_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, chunk_offset_t const offset, UpdateList &&updates,
    unsigned prefix_index, unsigned old_prefix_index)
{
    if (!old) {
        MONAD_DEBUG_ASSERT(prefix_index <= std::numeric_limits<uint8_t>::max());
        tnode->prefix_index = static_cast<uint8_t>(prefix_index);
        update_receiver receiver(
            &aux, sm.clone(), offset, std::move(updates), tnode);
        async_read(aux, std::move(receiver));
        return false;
    }
    assert(old_prefix_index != INVALID_PATH_INDEX);
    unsigned const old_psi = old_prefix_index;
    Requests requests;
    while (true) {
        tnode->relpath =
            NibblesView{old_psi, old_prefix_index, old->path_data()};
        if (updates.size() == 1 &&
            prefix_index == updates.front().key.nibble_size()) {
            return update_leaf_data_(aux, sm, old, tnode, updates.front());
        }
        unsigned const n = requests.split_into_sublists(
            std::move(updates), prefix_index); // NOLINT
        MONAD_DEBUG_ASSERT(n);
        if (old_prefix_index == old->path_nibble_index_end) {
            return dispatch_updates_flat_list_(
                aux, sm, old, tnode, requests, prefix_index);
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_prefix_index);
            n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++prefix_index;
            ++old_prefix_index;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        return mismatch_handler_(
            aux, sm, old, tnode, requests, old_prefix_index, prefix_index);
    }
}

/* dispatch updates at the end of old node's path. old node may have leaf data,
 * and there might be update to the leaf value. */
bool dispatch_updates_impl_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned prefix_index,
    std::optional<byte_string_view> const opt_leaf_data)
{
    tnode->init((old->mask | requests.mask), prefix_index, opt_leaf_data);
    unsigned const n = tnode->npending;

    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        MONAD_DEBUG_ASSERT(i <= std::numeric_limits<uint8_t>::max());
        if (bit & requests.mask) { // NOLINT
            Node *node = nullptr;
            if (bit & old->mask) {
                node_ptr next_ = old->next_ptr(old->to_child_index(i));
                MONAD_DEBUG_ASSERT(i <= std::numeric_limits<uint8_t>::max());
                auto next_tnode =
                    make_tnode(sm.get_state(), tnode, static_cast<uint8_t>(i));
                if (!upsert_(
                        aux,
                        sm,
                        next_.get(),
                        next_tnode.get(),
                        old->fnext(old->to_child_index(i)),
                        std::move(requests)[i],
                        prefix_index + 1,
                        next_ ? next_->bitpacked.path_nibble_index_start
                              : INVALID_PATH_INDEX)) {
                    if (next_tnode->opt_leaf_data.has_value()) {
                        next_tnode->old = std::move(next_);
                    }
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = create_new_trie_(
                    aux, sm, std::move(requests)[i], prefix_index + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = static_cast<uint8_t>(i);
                auto const len =
                    sm.get_compute().compute(child.data, child.ptr);
                MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
                child.len = static_cast<uint8_t>(len);
            }
            else {
                tnode->mask &= static_cast<uint16_t>(~bit);
            }
            --tnode->npending;
            ++j;
        }
        else if (bit & old->mask) {
            auto const old_child_index = old->to_child_index(i);
            if (old->next(old_child_index)) { // in memory, infers cached
                child.ptr = old->next_ptr(old_child_index).release();
            }
            auto const data = old->child_data_view(old->to_child_index(i));
            memcpy(&child.data, data.data(), data.size());
            MONAD_DEBUG_ASSERT(
                data.size() <= std::numeric_limits<uint8_t>::max());
            child.len = static_cast<uint8_t>(data.size());
            child.branch = static_cast<uint8_t>(i);
            child.offset = old->fnext(old_child_index);
            child.min_count = old->min_count(old_child_index);
            --tnode->npending;
            ++j;
        }
    }
    if (tnode->npending) {
        return false;
    }
    // no incarnation and no erase at this point
    return create_node_from_children_if_any_possibly_ondisk(
        aux, sm, tnode, prefix_index);
}

bool dispatch_updates_flat_list_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned prefix_index)
{
    auto &opt_leaf = requests.opt_leaf;
    auto opt_leaf_data = old->opt_value();
    if (opt_leaf.has_value()) {
        sm.forward();
        tnode->trie_section = sm.get_state();

        MONAD_ASSERT(opt_leaf->next.empty());
        if (opt_leaf.value().incarnation) {
            // incarnation means there are new children keys longer than curr
            // update's key
            MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
            tnode->node = create_new_trie_from_requests_(
                aux,
                sm,
                requests,
                tnode->relpath,
                prefix_index,
                opt_leaf.value().value);
            return true;
        }
        else if (opt_leaf.value().is_deletion()) {
            tnode->node = nullptr;
            return true;
        }
        if (opt_leaf.value().value.has_value()) {
            opt_leaf_data = opt_leaf.value().value;
        }
    }
    bool const finished = dispatch_updates_impl_(
        aux, sm, old, tnode, requests, prefix_index, opt_leaf_data);

    if (opt_leaf.has_value()) {
        sm.backward();
    }
    return finished;
}
// Split `old` at old_prefix_index, `updates` are already splitted at
// prefix_index to `requests`, which can have 1 or more sublists.
bool mismatch_handler_(
    UpdateAux &aux, TrieStateMachine &sm, Node *const old,
    UpwardTreeNode *tnode, Requests &requests, unsigned const old_prefix_index,
    unsigned const prefix_index)
{
    MONAD_DEBUG_ASSERT(old->has_relpath());
    // Note: no leaf can be created at an existing non-leaf node
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char const old_nibble =
        get_nibble(old->path_data(), old_prefix_index);
    tnode->init(
        static_cast<uint16_t>(1u << old_nibble | requests.mask), prefix_index);
    unsigned const n = tnode->npending;
    MONAD_DEBUG_ASSERT(n > 1);
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        if (bit & requests.mask) { // NOLINT
            Node *node = nullptr;
            if (i == old_nibble) {
                MONAD_DEBUG_ASSERT(i <= std::numeric_limits<uint8_t>::max());
                auto next_tnode =
                    make_tnode(sm.get_state(), tnode, static_cast<uint8_t>(i));
                if (!upsert_(
                        aux,
                        sm,
                        old,
                        next_tnode.get(),
                        {0, 0},
                        std::move(requests)[i],
                        prefix_index + 1,
                        old_prefix_index + 1)) {
                    MONAD_DEBUG_ASSERT(next_tnode->npending);
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = create_new_trie_(
                    aux, sm, std::move(requests)[i], prefix_index + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = static_cast<uint8_t>(i);
                auto const len =
                    sm.get_compute().compute(child.data, child.ptr);
                MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
                child.len = static_cast<uint8_t>(len);
            }
            else {
                tnode->mask &= static_cast<uint16_t>(~bit);
            }
            --tnode->npending;
            ++j;
        }
        else if (i == old_nibble) {
            // nexts[j] is a path-shortened old node, trim prefix
            NibblesView const relpath{
                old_prefix_index + 1,
                old->path_nibble_index_end,
                old->path_data()};
            // compute node hash
            child.ptr =
                update_node_diff_path_leaf(old, relpath, old->opt_value());
            child.branch = static_cast<uint8_t>(i);
            auto const len = sm.get_compute().compute(child.data, child.ptr);
            MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
            child.len = static_cast<uint8_t>(len);
            --tnode->npending;
            ++j;
        }
    }
    if (tnode->npending) {
        return false;
    }
    return create_node_from_children_if_any_possibly_ondisk(
        aux, sm, tnode, prefix_index);
}

node_writer_unique_ptr_type replace_node_writer(
    UpdateAux &aux, size_t bytes_yet_to_be_appended_to_existing = 0)
{
    node_writer_unique_ptr_type &node_writer = aux.node_writer;
    // Can't use add_to_offset(), because it asserts if we go past the
    // capacity
    auto offset_of_next_block = node_writer->sender().offset();
    file_offset_t offset = offset_of_next_block.offset;
    offset += node_writer->sender().written_buffer_bytes() +
              bytes_yet_to_be_appended_to_existing;
    offset_of_next_block.offset = offset & chunk_offset_t::max_offset;
    auto block_size = AsyncIO::WRITE_BUFFER_SIZE;
    auto const chunk_capacity = aux.io->chunk_capacity(offset_of_next_block.id);
    MONAD_ASSERT(offset <= chunk_capacity);
    if (offset == chunk_capacity) {
        auto *ci_ = aux.db_metadata()->free_list_begin();
        MONAD_ASSERT(ci_ != nullptr); // we are out of free blocks!
        auto idx = ci_->index(aux.db_metadata());
        aux.remove(idx);
        aux.append(UpdateAux::chunk_list::fast, idx);
        offset_of_next_block.id = idx & 0xfffffU;
        offset_of_next_block.offset = 0;
    }
    else if (offset + block_size > chunk_capacity) {
        block_size = chunk_capacity - offset;
    }
    auto ret = aux.io->make_connected(
        write_single_buffer_sender{
            offset_of_next_block, {(std::byte const *)nullptr, block_size}},
        write_operation_io_receiver{});
    return ret;
}

async_write_node_result async_write_node(UpdateAux &aux, Node *node)
{
    node_writer_unique_ptr_type &node_writer = aux.node_writer;
    aux.io->poll_nonblocking_if_not_within_completions(1);
    auto *sender = &node_writer->sender();
    auto const size = node->disk_size;
    auto const remaining_bytes = sender->remaining_buffer_bytes();
    async_write_node_result ret{
        .offset_written_to = INVALID_OFFSET,
        .bytes_appended = size,
        .io_state = node_writer.get()};
    [[likely]] if (size <= remaining_bytes) {
        ret.offset_written_to =
            sender->offset().add_to_offset(sender->written_buffer_bytes());
        auto *where_to_serialize = sender->advance_buffer_append(size);
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer((unsigned char *)where_to_serialize, node);
    }
    else {
        // renew write sender
        auto new_node_writer = replace_node_writer(aux, remaining_bytes);
        auto *new_sender = &new_node_writer->sender();
        auto *where_to_serialize = (unsigned char *)new_sender->buffer().data();
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer(where_to_serialize, node);
        // Corner case bug is avoided: when remaining_bytes = 0 and we reach the
        // end of chunk, which can happen inside else{} branch, if we use
        // add_to_offset(), it will exceed max_offset value and trigger the
        // assertion.
        if (new_sender->offset().id == sender->offset().id) {
            // In this branch, current node_writer won't be writing to the end
            // of chunk.
            ret.offset_written_to =
                sender->offset().add_to_offset(sender->written_buffer_bytes());
            // Move the front of new_sender into the tail of sender as they
            // share the same chunk
            auto *where_to_serialize2 =
                sender->advance_buffer_append(remaining_bytes);
            assert(where_to_serialize2 != nullptr);
            memcpy(where_to_serialize2, where_to_serialize, remaining_bytes);
            memmove(
                where_to_serialize,
                where_to_serialize + remaining_bytes,
                size - remaining_bytes);
            MONAD_ASSERT(
                new_sender->advance_buffer_append(size - remaining_bytes) !=
                nullptr);
        }
        else {
            // Don't split nodes across storage chunks, this simplifies reads
            // greatly
            MONAD_ASSERT(new_sender->written_buffer_bytes() == 0);
            MONAD_ASSERT(size <= new_sender->remaining_buffer_bytes());
            ret.offset_written_to = new_sender->offset();
            ret.io_state = new_node_writer.get();
            MONAD_ASSERT(new_sender->advance_buffer_append(size) != nullptr);
            // Pad buffer about to get initiated so it's O_DIRECT i/o aligned
            auto *tozero = sender->advance_buffer_append(remaining_bytes);
            assert(tozero != nullptr);
            memset(tozero, 0, remaining_bytes);
        }
        auto to_initiate = std::move(node_writer);
        node_writer = std::move(new_node_writer);
        to_initiate->initiate();
        // shall be recycled by the i/o receiver
        to_initiate.release();
    }
    return ret;
}

async_write_node_result
write_new_root_node(UpdateAux &aux, tnode_unique_ptr &root_tnode)
{
    node_writer_unique_ptr_type &node_writer = aux.node_writer;
    assert(root_tnode->node);

    auto const ret = async_write_node(aux, root_tnode->node);
    // Round up with all bits zero
    auto *sender = &node_writer->sender();
    auto written = sender->written_buffer_bytes();
    auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
    auto const tozerobytes = paddedup - written;
    auto *tozero = sender->advance_buffer_append(tozerobytes);
    assert(tozero != nullptr);
    memset(tozero, 0, tozerobytes);
    auto new_node_writer = replace_node_writer(aux);
    auto to_initiate = std::move(node_writer);
    node_writer = std::move(new_node_writer);
    to_initiate->initiate();
    // shall be recycled by the i/o receiver
    to_initiate.release();
    // flush async write root
    aux.io->flush();
    // write new root offset to the front of disk
    aux.advance_root_offset(ret.offset_written_to);
    return ret;
}

MONAD_MPT_NAMESPACE_END
