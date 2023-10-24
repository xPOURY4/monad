#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/upward_tnode.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

// invoke at the end of each block upsert
async_write_node_result
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode);

/* Names: `pi` is nibble index in prefix of an update,
 `old_pi` is nibble index of relpath in previous node - old.
 `*psi` is the starting nibble index in current function frame
*/
bool _dispatch_updates(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned pi);

bool _mismatch_handler(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned const old_pi, unsigned const pi);

Node *
_create_new_trie(UpdateAux &update_aux, UpdateList &&updates, unsigned pi = 0);

Node *_create_new_trie_from_requests(
    UpdateAux &update_aux, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data);

bool _upsert(
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
    MONAD_DEBUG_ASSERT(update_aux.current_list_dim == 0);
    auto root_tnode = make_tnode();
    if (!old) {
        root_tnode->node = _create_new_trie(update_aux, std::move(updates));
    }
    else {
        if (!_upsert(
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
    MONAD_DEBUG_ASSERT(update_aux.current_list_dim == 0);
    return node_ptr{root_tnode->node};
}

void upward_update(UpdateAux &update_aux, UpwardTreeNode *tnode, unsigned pi)
{
    bool beginning = true;
    while (!tnode->npending && tnode->parent) {
        auto parent_tnode = tnode->parent;
        if (beginning) {
            beginning = false;
        }
        else {
            MONAD_DEBUG_ASSERT(tnode->children.size()); // not a leaf
            if (!create_node_from_children_if_any_possibly_ondisk(
                    update_aux, tnode, pi)) {
                // create_node not finished, but issued an async read
                return;
            }
        }
        if (tnode->node) {
            pi -= tnode->node->path_nibbles_len() + 1;
            auto &entry = parent_tnode->children[tnode->child_j()];
            entry.branch = tnode->child_branch_bit;
            entry.ptr = tnode->node;
            entry.len = update_aux.comp.compute(entry.data, entry.ptr);
        }
        else { // node ends up being removed by erase updates
            parent_tnode->mask &= ~(1u << tnode->child_branch_bit);
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
    uint8_t pi;
    unsigned bytes_to_read;

    update_receiver(
        UpdateAux *_update_aux, chunk_offset_t offset, UpdateList &&_updates,
        UpwardTreeNode *_tnode, uint8_t _pi)
        : update_aux(_update_aux)
        , rd_offset(round_down_align<DISK_PAGE_BITS>(offset))
        , updates(std::move(_updates))
        , tnode(_tnode)
        , pi(_pi)
    {
        // prep uring data
        rd_offset.spare = 0;
        buffer_off = uint16_t(offset.offset - rd_offset.offset);
        auto const num_pages_to_load_node =
            offset.spare; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read = num_pages_to_load_node << DISK_PAGE_BITS;
    }

    void set_value(
        erased_connected_operation *,
        result<std::span<const std::byte>> _buffer)
    {
        MONAD_ASSERT(_buffer);
        std::span<const std::byte> buffer = std::move(_buffer).assume_value();
        // tnode owns the deserialized old node
        node_ptr old = deserialize_node_from_buffer(
            (unsigned char *)buffer.data() + buffer_off);
        Node *old_node = old.get();
        if (!_upsert(
                *update_aux,
                old_node,
                tnode,
                INVALID_OFFSET,
                std::move(updates),
                pi,
                old_node->bitpacked.path_nibble_index_start)) {
            if (tnode->opt_leaf_data.has_value()) {
                tnode->old = std::move(old);
            }
            return;
        }
        assert(tnode->npending == 0);
        upward_update(*update_aux, tnode, pi);
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
    uint8_t pi;

    create_node_receiver(
        UpdateAux *const _update_aux, UpwardTreeNode *const _tnode,
        uint8_t const _j, uint8_t const _pi)
        : update_aux(_update_aux)
        , rd_offset(0, 0)
        , tnode(_tnode)
        , j(_j)
        , pi(_pi)
    {
        // prep uring data
        auto offset = tnode->children[j].offset;
        rd_offset = round_down_align<DISK_PAGE_BITS>(offset);
        rd_offset.spare = 0;
        buffer_off = uint16_t(offset.offset - rd_offset.offset);
        auto const num_pages_to_load_node =
            offset.spare; // top 2 bits are for no_pages
        assert(num_pages_to_load_node <= 3);
        bytes_to_read = num_pages_to_load_node << DISK_PAGE_BITS;
    }

    void set_value(
        erased_connected_operation *,
        result<std::span<const std::byte>> _buffer)
    {
        MONAD_ASSERT(_buffer);
        std::span<const std::byte> buffer = std::move(_buffer).assume_value();
        // load node from read buffer
        tnode->node = create_coalesced_node_with_prefix(
            tnode->children[j].branch,
            deserialize_node_from_buffer(
                (unsigned char *)buffer.data() + buffer_off),
            tnode->relpath);
        upward_update(*update_aux, tnode, pi);
    }
};
static_assert(sizeof(create_node_receiver) == 32);
static_assert(alignof(create_node_receiver) == 8);

template <receiver Receiver>
void async_read(UpdateAux &update_aux, Receiver &&receiver)
{
    read_update_sender sender(receiver);
    assert(receiver.rd_offset < update_aux.node_writer->sender().offset());
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
        unsigned j = bitmask_index(orig_mask, std::countr_zero(mask));
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
                    async_write_node(
                        *update_aux.io, update_aux.node_writer, child.ptr)
                        .offset_written_to;
                child.offset.spare =
                    num_pages(child.offset.offset, child.ptr->get_disk_size());
                // free node if path longer than CACHE_LEVEL
                // do not free if n == 1, that's when parent is a leaf node with
                // branches
                bool const apply_cache = update_aux.current_list_dim ==
                                         update_aux.list_dim_to_apply_cache;
                bool const dispose_all = update_aux.current_list_dim >
                                         update_aux.list_dim_to_apply_cache;
                if (n > 1 && pi > 0 &&
                    (dispose_all ||
                     (apply_cache && (pi + 1 + child.ptr->path_nibbles_len() >
                                      CACHE_LEVEL)))) {
                    node_ptr{child.ptr};
                    child.ptr = nullptr;
                }
            }
        }
    }
    return create_node(update_aux.comp, mask, children, relpath, leaf_data);
}

bool create_node_from_children_if_any_possibly_ondisk(
    UpdateAux &update_aux, UpwardTreeNode *tnode, unsigned const pi)
{
    unsigned const n = bitmask_count(tnode->mask);
    if (n == 1 && !tnode->opt_leaf_data.has_value()) {
        unsigned const j =
            bitmask_index(tnode->orig_mask, std::countr_zero(tnode->mask));
        if (!tnode->children[j].ptr) {
            create_node_receiver receiver(&update_aux, tnode, j, pi);
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

//! get optional leaf data from optional new update and old leaf
std::optional<byte_string_view> _get_leaf_data(
    std::optional<Update> opt_update,
    std::optional<byte_string_view> const old_leaf = std::nullopt)
{
    if (opt_update.has_value() && opt_update.value().opt.has_value()) {
        return opt_update.value().opt;
    }
    return old_leaf;
}

//! update leaf data of old, old can have branches
bool _update_leaf_data(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Update const u)
{
    auto const &relpath = tnode->relpath;
    if (u.is_deletion()) {
        tnode->node = nullptr;
        return true;
    }
    if (u.next) {
        update_aux.current_list_dim++;
        Requests requests;
        requests.split_into_sublists(std::move(*(UpdateList *)u.next), 0);
        bool finished = true;
        if (u.incarnation) {
            tnode->node = _create_new_trie_from_requests(
                update_aux, requests, relpath, 0, _get_leaf_data(u));
        }
        else {
            finished = _dispatch_updates(update_aux, old, tnode, requests, 0);
        }
        update_aux.current_list_dim--;
        return finished;
    }
    tnode->node = u.incarnation
                      ? create_leaf(u.opt.value().data(), relpath)
                      : update_node_diff_path_leaf(
                            old, relpath, _get_leaf_data(u, old->opt_leaf()));
    return true;
}

// create a new trie from a list of updates, won't have incarnation
Node *_create_new_trie(UpdateAux &update_aux, UpdateList &&updates, unsigned pi)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &u = updates.front();
        MONAD_DEBUG_ASSERT(u.incarnation == false && u.opt.has_value());
        NibblesView const relpath{
            pi, (uint8_t)(2 * u.key.size()), u.key.data()};
        if (u.next) {
            update_aux.current_list_dim++;
            Requests requests;
            requests.split_into_sublists(std::move(*(UpdateList *)u.next), 0);
            MONAD_DEBUG_ASSERT(u.opt.has_value());
            auto ret = _create_new_trie_from_requests(
                update_aux, requests, relpath, 0, _get_leaf_data(u));
            update_aux.current_list_dim--;
            return ret;
        }
        return create_leaf(u.opt.value(), relpath);
    }
    Requests requests;
    uint8_t const psi = pi;
    while (requests.split_into_sublists(std::move(updates), pi) == 1 &&
           !requests.opt_leaf) {
        updates = std::move(requests).first_and_only_list();
        ++pi;
    }
    return _create_new_trie_from_requests(
        update_aux,
        requests,
        NibblesView{psi, pi, requests.get_first_path()},
        pi,
        _get_leaf_data(requests.opt_leaf));
}

Node *_create_new_trie_from_requests(
    UpdateAux &update_aux, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data)
{
    unsigned const n = bitmask_count(requests.mask);
    uint16_t const mask = requests.mask;
    MONAD_DEBUG_ASSERT(n > 0);
    ChildData children[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            auto node =
                _create_new_trie(update_aux, std::move(requests)[i], pi + 1);
            auto &entry = children[j++];
            entry.branch = i;
            entry.ptr = node;
            entry.len = update_aux.comp.compute(entry.data, entry.ptr);
        }
    }
    return create_node_from_children_if_any(
        update_aux, mask, mask, {children, n}, pi, relpath, opt_leaf_data);
}

bool _upsert(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    chunk_offset_t const offset, UpdateList &&updates, unsigned pi,
    unsigned old_pi)
{
    if (!old) {
        update_receiver receiver(
            &update_aux, offset, std::move(updates), tnode, pi);
        async_read(update_aux, std::move(receiver));
        return false;
    }
    assert(old_pi != INVALID_PATH_INDEX);
    unsigned const old_psi = old_pi;
    Requests requests;
    while (true) {
        tnode->relpath = NibblesView{old_psi, old_pi, old->path_data()};
        if (updates.size() == 1 && pi == updates.front().key.size() * 2) {
            return _update_leaf_data(update_aux, old, tnode, updates.front());
        }
        unsigned const n = requests.split_into_sublists(std::move(updates), pi);
        MONAD_DEBUG_ASSERT(n);
        if (old_pi == old->path_nibble_index_end) {
            return _dispatch_updates(update_aux, old, tnode, requests, pi);
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_pi);
            n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++pi;
            ++old_pi;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        return _mismatch_handler(update_aux, old, tnode, requests, old_pi, pi);
    }
}

//! dispatch updates at the end of old node's path
//! old node can have leaf data, there might be update to that leaf
//! return a new node
bool _dispatch_updates(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned pi)
{
    auto const &opt_leaf = requests.opt_leaf;
    if (opt_leaf.has_value() && opt_leaf.value().incarnation) {
        // incranation = 1, also have new children longer than curr update's key
        MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
        tnode->node = _create_new_trie_from_requests(
            update_aux, requests, tnode->relpath, pi, _get_leaf_data(opt_leaf));
        return true;
    }
    tnode->init(
        (old->mask | requests.mask), _get_leaf_data(opt_leaf, old->opt_leaf()));
    unsigned const n = tnode->npending;

    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        if (bit & requests.mask) {
            Node *node = nullptr;
            if (bit & old->mask) {
                node_ptr next_ = old->next_ptr(i);
                auto next_tnode = make_tnode();
                if (!_upsert(
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
                    next_tnode->link_parent(tnode, i);
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = _create_new_trie(
                    update_aux, std::move(requests)[i], pi + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = i;
                child.len = update_aux.comp.compute(child.data, child.ptr);
            }
            else {
                tnode->mask &= ~bit;
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
            child.len = data.size();
            child.branch = i;
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
bool _mismatch_handler(
    UpdateAux &update_aux, Node *const old, UpwardTreeNode *tnode,
    Requests &requests, unsigned const old_pi, unsigned const pi)
{
    MONAD_DEBUG_ASSERT(old->has_relpath());
    // Note: no leaf can be created at an existing non-leaf node
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char const old_nibble = get_nibble(old->path_data(), old_pi);
    tnode->init((1u << old_nibble | requests.mask));
    unsigned const n = tnode->npending;
    MONAD_DEBUG_ASSERT(n > 1);
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        auto &child = tnode->children[j];
        if (bit & requests.mask) {
            Node *node = nullptr;
            if (i == old_nibble) {
                auto next_tnode = make_tnode();
                if (!_upsert(
                        update_aux,
                        old,
                        next_tnode.get(),
                        {0, 0},
                        std::move(requests)[i],
                        pi + 1,
                        old_pi + 1)) {
                    next_tnode->child_branch_bit = i;
                    next_tnode->parent = tnode;
                    next_tnode.release();
                    ++j;
                    continue;
                }
                node = next_tnode->node;
            }
            else {
                node = _create_new_trie(
                    update_aux, std::move(requests)[i], pi + 1);
            }
            if (node) {
                child.ptr = node;
                child.branch = i;
                child.len = update_aux.comp.compute(child.data, child.ptr);
            }
            else {
                tnode->mask &= ~bit;
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
            child.branch = i;
            child.len = update_aux.comp.compute(child.data, child.ptr);
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
    AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    size_t bytes_yet_to_be_appended_to_existing = 0)
{
    // Can't use add_to_offset(), because it asserts if we go past the capacity
    auto offset_of_next_block = node_writer->sender().offset();
    file_offset_t offset = offset_of_next_block.offset;
    offset += node_writer->sender().written_buffer_bytes() +
              bytes_yet_to_be_appended_to_existing;
    offset_of_next_block.offset = offset;
    auto block_size = AsyncIO::WRITE_BUFFER_SIZE;
    const auto chunk_capacity = io.chunk_capacity(offset_of_next_block.id);
    assert(offset <= chunk_capacity);
    if (offset == chunk_capacity) {
        offset_of_next_block.id++;
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
    auto ret = io.make_connected(
        write_single_buffer_sender{
            offset_of_next_block, {(const std::byte *)nullptr, block_size}},
        write_operation_io_receiver{});
    return ret;
}

async_write_node_result async_write_node(
    AsyncIO &io, node_writer_unique_ptr_type &node_writer, Node *node)
{
    io.poll_nonblocking(1);
    auto *sender = &node_writer->sender();
    auto const size = node->disk_size;
    const async_write_node_result ret{
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
        auto new_node_writer =
            replace_node_writer(io, node_writer, remaining_bytes);
        auto to_initiate = std::move(node_writer);
        node_writer = std::move(new_node_writer);
        sender = &node_writer->sender();
        auto *where_to_serialize = (unsigned char *)sender->buffer().data();
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer(where_to_serialize, node);
        // Move the front of this into the tail of to_initiate
        auto *where_to_serialize2 =
            to_initiate->sender().advance_buffer_append(remaining_bytes);
        assert(where_to_serialize2 != nullptr);
        memcpy(where_to_serialize2, where_to_serialize, remaining_bytes);
        memmove(
            where_to_serialize,
            where_to_serialize + remaining_bytes,
            size - remaining_bytes);
        sender->advance_buffer_append(size - remaining_bytes);
        to_initiate->initiate();
        // shall be recycled by the i/o receiver
        to_initiate.release();
    }
    return ret;
}

async_write_node_result
write_new_root_node(UpdateAux &update_aux, tnode_unique_ptr &root_tnode)
{
    AsyncIO &io = *update_aux.io;
    node_writer_unique_ptr_type &node_writer = update_aux.node_writer;
    assert(root_tnode->node);

    auto const ret = async_write_node(io, node_writer, root_tnode->node);
    // Round up with all bits zero
    auto *sender = &node_writer->sender();
    auto written = sender->written_buffer_bytes();
    auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
    const auto tozerobytes = paddedup - written;
    auto *tozero = sender->advance_buffer_append(tozerobytes);
    assert(tozero != nullptr);
    memset(tozero, 0, tozerobytes);
    auto new_node_writer = replace_node_writer(io, node_writer);
    auto to_initiate = std::move(node_writer);
    node_writer = std::move(new_node_writer);
    to_initiate->initiate();
    // shall be recycled by the i/o receiver
    to_initiate.release();
    // flush async write root
    io.flush();
    // write new root offset to the front of disk
    update_aux.update_root_offset(ret.offset_written_to);
    return ret;
}

MONAD_MPT_NAMESPACE_END