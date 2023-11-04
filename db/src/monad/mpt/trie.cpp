#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/upward_tnode.hpp>

#include <monad/async/detail/scope_polyfill.hpp>

#include <monad/core/small_prng.hpp>

#include <cstdint>
#include <vector>

#include <sys/mman.h>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

std::pair<UpdateAux::chunk_list, uint32_t>
UpdateAux::chunk_list_and_age(uint32_t idx) const noexcept
{
    auto const *ci = db_metadata_[0]->at(idx);
    std::pair<chunk_list, uint32_t> ret(
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
    ret.second &= 0xfffff;
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

void UpdateAux::rewind_root_offset_to(chunk_offset_t offset)
{
    // TODO FIXME: We need to also adjust the slow list
    auto *ci = db_metadata_[0]->fast_list_begin();
    for (; ci != nullptr && offset.id != ci->index(db_metadata_[0]);
         ci = ci->next(db_metadata_[0])) {
    }
    // If this trips, the supplied root offset is not in the fast list
    MONAD_ASSERT(ci != nullptr);
    while (ci != db_metadata_[0]->fast_list_end()) {
        auto const idx = db_metadata_[0]->fast_list.end;
        remove(idx);
        io->storage_pool().chunk(storage_pool::seq, idx)->destroy_contents();
        prepend(chunk_list::free, idx);
    }
    {
        auto g = db_metadata_[0]->hold_dirty();
        db_metadata_[0]->root_offset = offset;
    }
    {
        auto g = db_metadata_[1]->hold_dirty();
        db_metadata_[1]->root_offset = offset;
    }
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
        struct prng_wrapper : small_prng
        {
            using result_type = uint32_t;
            static constexpr result_type min()
            {
                return 0;
            }
            static constexpr result_type max()
            {
                return UINT32_MAX;
            }
        } rand;
        std::shuffle(chunks.begin(), chunks.end(), rand);
        chunk_offset_t const root_offset(chunks.back(), 0);
        append(chunk_list::fast, root_offset.id);
        chunks.pop_back();
        for (uint32_t i : chunks) {
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
    // Make sure the root offset points into a block in use as a sanity check
    auto const root_offset = get_root_offset();
    auto chunk = io->storage_pool().chunk(storage_pool::seq, root_offset.id);
    MONAD_ASSERT(chunk->size() >= root_offset.offset);
    /* The DB may have trailing garbage if it died when writing the next block.
    We simply ignore it, it'll get compacted at some later point.
    */
    chunk_offset_t node_writer_offset(root_offset);
    auto *last_chunk_info = db_metadata_[0]->fast_list_end();
    if (last_chunk_info != nullptr &&
        last_chunk_info->index(db_metadata()) != node_writer_offset.id) {
        node_writer_offset.id =
            last_chunk_info->index(db_metadata()) & 0xfffffU;
        chunk =
            io->storage_pool().chunk(storage_pool::seq, node_writer_offset.id);
    }
    node_writer_offset.offset = chunk->size() & 0xfffffU;
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
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode);

/* Names: `pi` is nibble index in prefix of an update,
 `old_pi` is nibble index of relpath in previous node - old.
 `*psi` is the starting nibble index in current function frame
*/
bool dispatch_updates_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned pi);

bool mismatch_handler_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned const old_pi, unsigned const pi);

Node *
create_new_trie_(UpdateAux &update_aux, UpdateList &&updates, unsigned pi = 0);

Node *create_new_trie_from_requests_(
    UpdateAux &update_aux, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data);

bool upsert_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    chunk_offset_t const old_offset, UpdateList &&updates, unsigned pi = 0,
    unsigned old_pi = 0);

void write_node_and_compute_hash(
    ChildData &dest, UpdateAux &update_aux, Node *const node, uint8_t const i,
    unsigned const parent_pi);

bool create_node_from_children_if_any_possibly_ondisk(
    UpdateAux &update_aux, UpwardTreeNode *tnode, unsigned const pi);

node_ptr upsert(UpdateAux &update_aux, Node *const old, UpdateList &&updates)
{
    update_aux.sm->reset();
    auto root_tnode = make_tnode(update_aux.trie_section());
    if (!old) {
        root_tnode->node = create_new_trie_(update_aux, std::move(updates));
    }
    else {
        if (!upsert_(
                update_aux,
                old,
                root_tnode.get(),
                INVALID_OFFSET,
                std::move(updates))) {
            assert(update_aux.is_on_disk());
            update_aux.io->flush();
            MONAD_ASSERT(create_node_from_children_if_any_possibly_ondisk(
                update_aux, root_tnode.get(), 0));
        }
        if (!root_tnode->node) {
            return {};
        }
    }
    if (update_aux.is_on_disk()) {
        write_new_root_node(update_aux, root_tnode);
    }
    return node_ptr{root_tnode->node};
}

void upward_update(UpdateAux &update_aux, UpwardTreeNode *tnode)
{
    bool beginning = true;
    while (!tnode->npending && tnode->parent) {
        auto parent_tnode = tnode->parent;
        update_aux.sm->reset(tnode->trie_section);
        if (beginning) {
            beginning = false;
        }
        else {
            MONAD_DEBUG_ASSERT(tnode->children.size()); // not a leaf
            if (!create_node_from_children_if_any_possibly_ondisk(
                    update_aux, tnode, tnode->pi)) {
                // create_node not finished, but issued an async read
                return;
            }
        }
        if (tnode->node) {
            auto &entry = parent_tnode->children[tnode->child_j()];
            entry.branch = tnode->child_branch_bit;
            entry.ptr = tnode->node;
            auto const len = update_aux.comp().compute(entry.data, entry.ptr);
            MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
            entry.len = static_cast<uint8_t>(len);
        }
        else { // node ends up being removed by erase updates
            parent_tnode->mask &=
                static_cast<uint16_t>(~(1u << tnode->child_branch_bit));
        }
        --parent_tnode->npending;
        UpwardTreeNode *p = parent_tnode;
        tnode_unique_ptr{tnode};
        tnode = p;
    }
}

struct update_receiver
{
    UpdateAux *update_aux;
    chunk_offset_t rd_offset;
    UpdateList updates;
    UpwardTreeNode *tnode;
    uint16_t buffer_off;
    unsigned bytes_to_read;

    update_receiver(
        UpdateAux *update_aux_, chunk_offset_t offset, UpdateList &&updates_,
        UpwardTreeNode *tnode_)
        : update_aux(update_aux_)
        , rd_offset(round_down_align<DISK_PAGE_BITS>(offset))
        , updates(std::move(updates_))
        , tnode(tnode_)
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
        std::span<std::byte const> buffer = std::move(buffer_).assume_value();
        // tnode owns the deserialized old node
        node_ptr old = deserialize_node_from_buffer(
            (unsigned char *)buffer.data() + buffer_off);
        Node *old_node = old.get();
        update_aux->sm->reset(tnode->trie_section);
        if (!upsert_(
                *update_aux,
                old_node,
                tnode,
                INVALID_OFFSET,
                std::move(updates),
                tnode->pi,
                old_node->bitpacked.path_nibble_index_start)) {
            if (tnode->opt_leaf_data.has_value()) {
                tnode->old = std::move(old);
            }
            return;
        }
        assert(tnode->npending == 0);
        upward_update(*update_aux, tnode);
    }
};
static_assert(sizeof(update_receiver) == 48);
static_assert(alignof(update_receiver) == 8);

struct create_node_receiver
{
    UpdateAux *update_aux;
    chunk_offset_t rd_offset;
    UpwardTreeNode *tnode;
    unsigned bytes_to_read;
    uint16_t buffer_off;
    uint8_t j;

    create_node_receiver(
        UpdateAux *const update_aux_, UpwardTreeNode *const tnode_,
        uint8_t const j_)
        : update_aux(update_aux_)
        , rd_offset(0, 0)
        , tnode(tnode_)
        , j(j_)
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
        std::span<std::byte const> buffer = std::move(buffer_).assume_value();
        // load node from read buffer
        tnode->node = create_coalesced_node_with_prefix(
            tnode->children[j].branch,
            deserialize_node_from_buffer(
                (unsigned char *)buffer.data() + buffer_off),
            tnode->relpath);
        upward_update(*update_aux, tnode);
    }
};
static_assert(sizeof(create_node_receiver) == 32);
static_assert(alignof(create_node_receiver) == 8);

template <receiver Receiver>
void async_read(UpdateAux &update_aux, Receiver &&receiver)
{
    read_update_sender sender(receiver);
    auto iostate =
        update_aux.io->make_connected(std::move(sender), std::move(receiver));
    iostate->initiate();
    // TEMPORARY UNTIL ALL THIS GETS BROKEN OUT: Release
    // management until i/o completes
    iostate.release();
}

// helpers
Node *create_node_from_children_if_any(
    UpdateAux &update_aux, uint16_t const orig_mask, uint16_t const mask,
    std::span<ChildData> children, unsigned const pi, NibblesView const relpath,
    std::optional<byte_string_view> const leaf_data = std::nullopt)
{
    // handle non child and single child cases
    unsigned const n = bitmask_count(mask);
    if (n == 0) {
        return leaf_data.has_value() ? create_leaf(leaf_data.value(), relpath)
                                     : nullptr;
    }
    else if (n == 1 && !leaf_data.has_value()) {
        auto const j = bitmask_index(
            orig_mask, static_cast<unsigned>(std::countr_zero(mask)));
        assert(children[j].ptr);
        return create_coalesced_node_with_prefix(
            children[j].branch, node_ptr{children[j].ptr}, relpath);
    }
    MONAD_DEBUG_ASSERT(n > 1 || (n == 1 && leaf_data.has_value()));
    // write children to disk, free any if exceeds the cache level limit
    if (update_aux.is_on_disk()) {
        for (auto &child : children) {
            if (child.branch != INVALID_BRANCH &&
                child.offset == INVALID_OFFSET) {
                child.offset =
                    async_write_node(update_aux, child.ptr).offset_written_to;
                auto const pages =
                    num_pages(child.offset.offset, child.ptr->get_disk_size());
                MONAD_DEBUG_ASSERT(
                    pages <= std::numeric_limits<uint16_t>::max());
                child.offset.spare = static_cast<uint16_t>(pages);
                // free node if path longer than CACHE_LEVEL
                // do not free if n == 1, that's when parent is a leaf node with
                // branches
                auto cache_opt = update_aux.sm->cache_option();
                if (n > 1 && pi > 0 &&
                    (cache_opt == CacheOption::DisposeAll ||
                     (cache_opt == CacheOption::ApplyLevelBasedCache &&
                      (pi + 1 + child.ptr->path_nibbles_len() >
                       CACHE_LEVEL)))) {
                    node_ptr{child.ptr};
                    child.ptr = nullptr;
                }
            }
        }
    }
    return create_node(update_aux.comp(), mask, children, relpath, leaf_data);
}

bool create_node_from_children_if_any_possibly_ondisk(
    UpdateAux &update_aux, UpwardTreeNode *tnode, unsigned const pi)
{
    unsigned const n = bitmask_count(tnode->mask);
    if (n == 1 && !tnode->opt_leaf_data.has_value()) {
        auto const j = bitmask_index(
            tnode->orig_mask,
            static_cast<unsigned>(std::countr_zero(tnode->mask)));
        if (!tnode->children[j].ptr) {
            MONAD_DEBUG_ASSERT(pi <= std::numeric_limits<uint8_t>::max());
            tnode->pi = static_cast<uint8_t>(pi);
            create_node_receiver receiver(
                &update_aux, tnode, static_cast<uint8_t>(j));
            async_read(update_aux, std::move(receiver));
            return false;
        }
    }
    tnode->node = create_node_from_children_if_any(
        update_aux,
        tnode->orig_mask,
        tnode->mask,
        tnode->children,
        pi,
        tnode->relpath,
        tnode->opt_leaf_data);
    return true;
}

//! update leaf data of old, old can have branches
bool update_leaf_data_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Update &update)
{
    auto const &relpath = tnode->relpath;
    if (update.is_deletion()) {
        tnode->node = nullptr;
        return true;
    }
    if (update.next.has_value()) {
        update_aux.sm->forward();
        tnode->trie_section = update_aux.trie_section();
        Requests requests;
        requests.split_into_sublists(std::move(update.next.value()), 0);
        bool finished = true;
        if (update.incarnation) {
            tnode->node = create_new_trie_from_requests_(
                update_aux, requests, relpath, 0, update.value);
        }
        else {
            finished = dispatch_updates_(update_aux, old, tnode, requests, 0);
        }
        update_aux.sm->backward();
        return finished;
    }
    tnode->node =
        update.incarnation
            ? create_leaf(update.value.value().data(), relpath)
            : update_node_diff_path_leaf(
                  old,
                  relpath,
                  update.value.has_value() ? update.value : old->opt_leaf());
    return true;
}

// create a new trie from a list of updates, won't have incarnation
Node *create_new_trie_(UpdateAux &update_aux, UpdateList &&updates, unsigned pi)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &update = updates.front();
        MONAD_DEBUG_ASSERT(
            update.incarnation == false && update.value.has_value());
        auto const relpath = update.key.substr(pi);
        if (update.next.has_value()) {
            update_aux.sm->forward();
            Requests requests;
            requests.split_into_sublists(std::move(update.next.value()), 0);
            MONAD_DEBUG_ASSERT(update.value.has_value());
            auto ret = create_new_trie_from_requests_(
                update_aux, requests, relpath, 0, update.value);
            update_aux.sm->backward();
            return ret;
        }
        return create_leaf(update.value.value(), relpath);
    }
    Requests requests;
    auto const psi = pi;
    while (requests.split_into_sublists(std::move(updates), pi) == 1 &&
           !requests.opt_leaf) {
        updates = std::move(requests).first_and_only_list();
        ++pi;
    }
    return create_new_trie_from_requests_(
        update_aux,
        requests,
        requests.get_first_path().substr(psi, pi - psi),
        pi,
        requests.opt_leaf.and_then(&Update::value));
}

Node *create_new_trie_from_requests_(
    UpdateAux &update_aux, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data)
{
    unsigned const n = bitmask_count(requests.mask);
    uint16_t const mask = requests.mask;
    MONAD_DEBUG_ASSERT(n > 0);
    std::vector<ChildData> children(n);
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            auto node =
                create_new_trie_(update_aux, std::move(requests)[i], pi + 1);
            auto &entry = children[j++];
            entry.branch = static_cast<uint8_t>(i);
            entry.ptr = node;
            auto const len = update_aux.comp().compute(entry.data, entry.ptr);
            MONAD_DEBUG_ASSERT(len <= std::numeric_limits<uint8_t>::max());
            entry.len = static_cast<uint8_t>(len);
        }
    }
    return create_node_from_children_if_any(
        update_aux,
        mask,
        mask,
        std::span{children},
        pi,
        relpath,
        opt_leaf_data);
}

bool upsert_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    chunk_offset_t const offset, UpdateList &&updates, unsigned pi,
    unsigned old_pi)
{
    if (!old) {
        MONAD_DEBUG_ASSERT(pi <= std::numeric_limits<uint8_t>::max());
        tnode->pi = static_cast<uint8_t>(pi);
        update_receiver receiver(
            &update_aux, offset, std::move(updates), tnode);
        async_read(update_aux, std::move(receiver));
        return false;
    }
    assert(old_pi != INVALID_PATH_INDEX);
    unsigned const old_psi = old_pi;
    Requests requests;
    while (true) {
        tnode->relpath = NibblesView{old_psi, old_pi, old->path_data()};
        if (updates.size() == 1 && pi == updates.front().key.nibble_size()) {
            return update_leaf_data_(update_aux, old, tnode, updates.front());
        }
        unsigned const n = requests.split_into_sublists(std::move(updates), pi);
        MONAD_DEBUG_ASSERT(n);
        if (old_pi == old->path_nibble_index_end) {
            return dispatch_updates_(update_aux, old, tnode, requests, pi);
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_pi);
            n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++pi;
            ++old_pi;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        return mismatch_handler_(update_aux, old, tnode, requests, old_pi, pi);
    }
}

//! dispatch updates at the end of old node's path
//! old node can have leaf data, there might be update to that leaf
//! return a new node
bool dispatch_updates_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned pi)
{
    auto const &opt_leaf = requests.opt_leaf;
    if (opt_leaf.has_value()) {
        if (opt_leaf.value().incarnation) {
            // incarnation = 1, also have new children longer than curr update's
            // key
            MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
            tnode->node = create_new_trie_from_requests_(
                update_aux,
                requests,
                tnode->relpath,
                pi,
                opt_leaf.value().value);
            return true;
        }
        else if (opt_leaf.value().is_deletion()) {
            tnode->node = nullptr;
            return true;
        }
    }
    tnode->init(
        (old->mask | requests.mask),
        pi,
        (opt_leaf.has_value() && opt_leaf.value().value.has_value())
            ? opt_leaf.value().value
            : old->opt_leaf());
    unsigned const n = tnode->npending;

    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        MONAD_DEBUG_ASSERT(i <= std::numeric_limits<uint8_t>::max());
        if (bit & requests.mask) {
            Node *node = nullptr;
            if (bit & old->mask) {
                node_ptr next_ = old->next_ptr(i);
                auto next_tnode = make_tnode(update_aux.trie_section());
                if (!upsert_(
                        update_aux,
                        next_.get(),
                        next_tnode.get(),
                        old->fnext(i),
                        std::move(requests)[i],
                        pi + 1,
                        next_ ? next_->bitpacked.path_nibble_index_start
                              : INVALID_PATH_INDEX)) {
                    // always link parent after recurse down
                    if (next_tnode->opt_leaf_data.has_value()) {
                        next_tnode->old = std::move(next_);
                    }
                    next_tnode->link_parent(tnode, static_cast<uint8_t>(i));
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = create_new_trie_(
                    update_aux, std::move(requests)[i], pi + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = static_cast<uint8_t>(i);
                auto const len =
                    update_aux.comp().compute(child.data, child.ptr);
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
            if (old->next(i)) { // in memory, infers cached
                child.ptr = old->next_ptr(i).release();
            }
            auto const data = old->child_data_view(i);
            memcpy(&child.data, data.data(), data.size());
            MONAD_DEBUG_ASSERT(
                data.size() <= std::numeric_limits<uint8_t>::max());
            child.len = static_cast<uint8_t>(data.size());
            child.branch = static_cast<uint8_t>(i);
            child.offset = old->fnext(i);
            --tnode->npending;
            ++j;
        }
    }
    // debug
    for (unsigned j = 0; j < old->n(); ++j) {
        assert(!old->next_j(j));
    }
    if (tnode->npending) {
        return false;
    }
    // no incarnation and no erase at this point
    return create_node_from_children_if_any_possibly_ondisk(
        update_aux, tnode, pi);
}

//! split old at old_pi, updates at pi
//! requests can have 1 or more sublists
bool mismatch_handler_(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned const old_pi, unsigned const pi)
{
    MONAD_DEBUG_ASSERT(old->has_relpath());
    // Note: no leaf can be created at an existing non-leaf node
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char const old_nibble = get_nibble(old->path_data(), old_pi);
    tnode->init(static_cast<uint16_t>(1u << old_nibble | requests.mask), pi);
    unsigned const n = tnode->npending;
    MONAD_DEBUG_ASSERT(n > 1);
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        if (bit & requests.mask) {
            Node *node = nullptr;
            if (i == old_nibble) {
                auto next_tnode = make_tnode(update_aux.trie_section());
                if (!upsert_(
                        update_aux,
                        old,
                        next_tnode.get(),
                        {0, 0},
                        std::move(requests)[i],
                        pi + 1,
                        old_pi + 1)) {
                    MONAD_ASSERT(next_tnode->npending);
                    MONAD_DEBUG_ASSERT(
                        i <= std::numeric_limits<uint8_t>::max());
                    next_tnode->link_parent(tnode, static_cast<uint8_t>(i));
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = create_new_trie_(
                    update_aux, std::move(requests)[i], pi + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = static_cast<uint8_t>(i);
                auto const len =
                    update_aux.comp().compute(child.data, child.ptr);
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
            NibblesView relpath{
                old_pi + 1, old->path_nibble_index_end, old->path_data()};
            // compute node hash
            child.ptr =
                update_node_diff_path_leaf(old, relpath, old->opt_leaf());
            child.branch = static_cast<uint8_t>(i);
            auto const len = update_aux.comp().compute(child.data, child.ptr);
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
        update_aux, tnode, pi);
}

node_writer_unique_ptr_type replace_node_writer(
    UpdateAux &aux, size_t bytes_yet_to_be_appended_to_existing = 0)
{
    node_writer_unique_ptr_type &node_writer = aux.node_writer;
    // Can't use add_to_offset(), because it asserts if we go past the capacity
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
    /*printf(
        "*** replace_node_writer next write buffer will use chunk %u offset "
        "%u size %lu\n",
        offset_of_next_block.id,
        offset_of_next_block.offset,
        block_size);*/
    auto ret = aux.io->make_connected(
        write_single_buffer_sender{
            offset_of_next_block, {(std::byte const *)nullptr, block_size}},
        write_operation_io_receiver{});
    return ret;
}

async_write_node_result async_write_node(UpdateAux &aux, Node *node)
{
    node_writer_unique_ptr_type &node_writer = aux.node_writer;
    aux.io->poll_nonblocking(1);
    auto *sender = &node_writer->sender();
    auto const size = node->disk_size;
    async_write_node_result ret{
        sender->offset().add_to_offset(sender->written_buffer_bytes()),
        size,
        node_writer.get()};
    auto const remaining_bytes = sender->remaining_buffer_bytes();
    [[likely]] if (size <= remaining_bytes) {
        auto *where_to_serialize = sender->advance_buffer_append(size);
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer((unsigned char *)where_to_serialize, node);
    }
    else {
        // renew write sender
        auto new_node_writer = replace_node_writer(aux, remaining_bytes);
        auto to_initiate = std::move(node_writer);
        node_writer = std::move(new_node_writer);
        sender = &node_writer->sender(); // new sender
        auto *where_to_serialize = (unsigned char *)sender->buffer().data();
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer(where_to_serialize, node);
        if (node_writer->sender().offset().id ==
            to_initiate->sender().offset().id) {
            // Move the front of this into the tail of to_initiate as they share
            // the same chunk
            auto *where_to_serialize2 =
                to_initiate->sender().advance_buffer_append(remaining_bytes);
            assert(where_to_serialize2 != nullptr);
            memcpy(where_to_serialize2, where_to_serialize, remaining_bytes);
            memmove(
                where_to_serialize,
                where_to_serialize + remaining_bytes,
                size - remaining_bytes);
            sender->advance_buffer_append(size - remaining_bytes);
        }
        else {
            // Don't split nodes across storage chunks, this simplifies reads
            // greatly
            MONAD_ASSERT(sender->written_buffer_bytes() == 0);
            ret = async_write_node_result{
                sender->offset(), size, node_writer.get()};
            sender->advance_buffer_append(size);
            // Pad buffer about to get initiated so it's O_DIRECT i/o aligned
            auto *tozero =
                to_initiate->sender().advance_buffer_append(remaining_bytes);
            assert(tozero != nullptr);
            memset(tozero, 0, remaining_bytes);
        }
        to_initiate->initiate();
        // shall be recycled by the i/o receiver
        to_initiate.release();
    }
    return ret;
}

async_write_node_result
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode)
{
    node_writer_unique_ptr_type &node_writer = update_aux.node_writer;
    assert(root_tnode->node);

    auto const ret = async_write_node(update_aux, root_tnode->node);
    // Round up with all bits zero
    auto *sender = &node_writer->sender();
    auto written = sender->written_buffer_bytes();
    auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
    auto const tozerobytes = paddedup - written;
    auto *tozero = sender->advance_buffer_append(tozerobytes);
    assert(tozero != nullptr);
    memset(tozero, 0, tozerobytes);
    auto new_node_writer = replace_node_writer(update_aux);
    auto to_initiate = std::move(node_writer);
    node_writer = std::move(new_node_writer);
    to_initiate->initiate();
    // shall be recycled by the i/o receiver
    to_initiate.release();
    // flush async write root
    update_aux.io->flush();
    // write new root offset to the front of disk
    update_aux.advance_root_offset(ret.offset_written_to);
    return ret;
}

MONAD_MPT_NAMESPACE_END
