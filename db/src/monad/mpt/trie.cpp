#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/detail/start_lifetime_as_polyfill.hpp>
#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/io_senders.hpp>
#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
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

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

/* Names: `prefix_index` is nibble index in prefix of an update,
 `old_prefix_index` is nibble index of path in previous node - old.
 `*_prefix_index_start` is the starting nibble index in current function frame
*/
void dispatch_updates_flat_list_(
    UpdateAux &, TrieStateMachine &, UpwardTreeNode &parent, ChildData &,
    Node::UniquePtr old, Requests &, NibblesView path, unsigned prefix_index);

void dispatch_updates_impl_(
    UpdateAux &, TrieStateMachine &, UpwardTreeNode &parent, ChildData &,
    Node::UniquePtr old, Requests &, unsigned prefix_index, NibblesView path,
    std::optional<byte_string_view> opt_leaf_data);

void mismatch_handler_(
    UpdateAux &, TrieStateMachine &, UpwardTreeNode &parent, ChildData &,
    Node::UniquePtr old, Requests &, NibblesView path,
    unsigned old_prefix_index, unsigned prefix_index);

void create_new_trie_(
    UpdateAux &aux, TrieStateMachine &sm, ChildData &entry,
    UpdateList &&updates, unsigned prefix_index = 0);

void create_new_trie_from_requests_(
    UpdateAux &, TrieStateMachine &, ChildData &, Requests &, NibblesView path,
    unsigned prefix_index, std::optional<byte_string_view> opt_leaf_data);

void upsert_(
    UpdateAux &, TrieStateMachine &, UpwardTreeNode &parent, ChildData &,
    Node::UniquePtr old, UpdateList &&, unsigned prefix_index = 0,
    unsigned old_prefix_index = 0);

void create_node_compute_data_possibly_async(
    UpdateAux &, TrieStateMachine &, UpwardTreeNode &parent, ChildData &,
    tnode_unique_ptr, bool might_on_disk = true);

struct async_write_node_result
{
    chunk_offset_t offset_written_to;
    unsigned bytes_appended;
    erased_connected_operation *io_state;
};

// invoke at the end of each block upsert
async_write_node_result write_new_root_node(UpdateAux &, Node const &);

Node::UniquePtr upsert(
    UpdateAux &aux, TrieStateMachine &sm, Node::UniquePtr old,
    UpdateList &&updates)
{
    sm.reset();
    auto sentinel = make_tnode(1 /*mask*/, 0 /*prefix_index*/, sm.get_state());
    ChildData &entry = sentinel->children[0];
    entry.set_branch_and_section(0, sm.get_state());
    if (old) {
        upsert_(aux, sm, *sentinel, entry, std::move(old), std::move(updates));
        if (sentinel->npending) {
            aux.io->flush();
            MONAD_ASSERT(sentinel->npending == 0);
        }
    }
    else {
        create_new_trie_(aux, sm, entry, std::move(updates));
    }
    auto *const root = entry.ptr;
    if (root && aux.is_on_disk()) {
        write_new_root_node(aux, *root);
    }
    return Node::UniquePtr{root};
}

/////////////////////////////////////////////////////
// Async read and update
/////////////////////////////////////////////////////

// Upward update until a unfinished parent node. For each tnode, create the
// trie Node when all its children are created
void upward_update(UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode *tnode)
{
    while (!tnode->npending && tnode->parent) {
        MONAD_ASSERT(tnode);
        MONAD_DEBUG_ASSERT(tnode->children.size()); // not a leaf
        auto *parent = tnode->parent;
        sm.reset(tnode->trie_section);
        auto &entry = parent->children[tnode->child_index()];
        // put created node and compute to entry in parent
        create_node_compute_data_possibly_async(
            aux, sm, *parent, entry, tnode_unique_ptr{tnode});
        tnode = parent;
    }
}

struct update_receiver
{
    UpdateAux *aux;
    std::unique_ptr<TrieStateMachine> sm;
    UpdateList updates;
    UpwardTreeNode *parent;
    ChildData &entry;
    chunk_offset_t rd_offset;
    unsigned bytes_to_read;
    uint16_t buffer_off;
    uint8_t prefix_index;

    update_receiver(
        UpdateAux *aux, std::unique_ptr<TrieStateMachine> sm, ChildData &entry,
        chunk_offset_t offset, UpdateList &&updates, UpwardTreeNode *parent,
        unsigned const prefix_index)
        : aux(aux)
        , sm(std::move(sm))
        , updates(std::move(updates))
        , parent(parent)
        , entry(entry)
        , rd_offset(round_down_align<DISK_PAGE_BITS>(offset))
        , prefix_index(static_cast<uint8_t>(prefix_index))
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
        auto old = deserialize_node_from_buffer(
            (unsigned char *)buffer.data() + buffer_off);
        // continue recurse down the trie starting from `old`
        unsigned const old_prefix_index = old->path_start_nibble();
        MONAD_ASSERT(sm->get_state() == parent->trie_section);
        upsert_(
            *aux,
            *sm,
            *parent,
            entry,
            std::move(old),
            std::move(updates),
            prefix_index,
            old_prefix_index);
        upward_update(*aux, *sm, parent);
    }
};

static_assert(sizeof(update_receiver) == 64);
static_assert(alignof(update_receiver) == 8);

struct read_single_child_receiver
{
    UpdateAux *aux;
    chunk_offset_t rd_offset;
    UpwardTreeNode *tnode; // single child tnode
    ChildData &child;
    unsigned bytes_to_read;
    uint16_t buffer_off;
    std::unique_ptr<TrieStateMachine> sm;

    read_single_child_receiver(
        UpdateAux *const aux, std::unique_ptr<TrieStateMachine> sm,
        UpwardTreeNode *const tnode, ChildData &child)
        : aux(aux)
        , rd_offset(0, 0)
        , tnode(tnode)
        , child(child)
        , sm(std::move(sm))
    {
        // prep uring data
        auto offset = child.offset;
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
        auto *parent = tnode->parent;
        MONAD_DEBUG_ASSERT(parent);
        auto &entry = parent->children[tnode->child_index()];
        MONAD_DEBUG_ASSERT(entry.branch < 16);
        auto &child = tnode->children[bitmask_index(
            tnode->orig_mask,
            static_cast<unsigned>(std::countr_zero(tnode->mask)))];
        child.ptr = deserialize_node_from_buffer(
                        (unsigned char *)buffer.data() + buffer_off)
                        .release();
        create_node_compute_data_possibly_async(
            *aux, *sm, *parent, entry, tnode_unique_ptr{tnode}, false);
        upward_update(*aux, *sm, parent);
    }
};

static_assert(sizeof(read_single_child_receiver) == 48);
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

/////////////////////////////////////////////////////
// Create Node
/////////////////////////////////////////////////////
Node *create_node_from_children_if_any(
    UpdateAux &aux, TrieStateMachine &sm, uint16_t const orig_mask,
    uint16_t const mask, std::span<ChildData> children,
    unsigned const prefix_index, NibblesView const path,
    std::optional<byte_string_view> const leaf_data = std::nullopt)
{
    // handle non child and single child cases
    auto const number_of_children = static_cast<unsigned>(std::popcount(mask));
    if (number_of_children == 0) {
        return leaf_data.has_value()
                   ? make_node(0, {}, path, leaf_data.value(), {}).release()
                   : nullptr;
    }
    else if (number_of_children == 1 && !leaf_data.has_value()) {
        auto const j = bitmask_index(
            orig_mask, static_cast<unsigned>(std::countr_zero(mask)));
        MONAD_DEBUG_ASSERT(children[j].ptr);
        auto const node = Node::UniquePtr{children[j].ptr};
        /* Note: there's a potential superfluous extension hash recomputation
        when node coaleases upon erases, because we compute node hash when path
        is not yet the final form. There's not yet a good way to avoid this
        unless we delay all the compute() after all child branches finish
        creating nodes and return in the recursion */
        return make_node(
                   *node,
                   concat(path, children[j].branch, node->path_nibble_view()),
                   node->has_value() ? std::make_optional(node->value())
                                     : std::nullopt)
            .release();
    }
    MONAD_DEBUG_ASSERT(
        number_of_children > 1 ||
        (number_of_children == 1 && leaf_data.has_value()));
    // write children to disk, free any if exceeds the cache level limit
    if (aux.is_on_disk()) {
        for (auto &child : children) {
            if (child.is_valid() && child.offset == INVALID_OFFSET) {
                // won't duplicate write of unchanged old child
                MONAD_DEBUG_ASSERT(child.branch < 16);
                MONAD_DEBUG_ASSERT(child.ptr);
                MONAD_DEBUG_ASSERT(child.min_count == uint32_t(-1));
                child.offset =
                    async_write_node_set_spare(aux, *child.ptr, true);
                child.min_count = calc_min_count(
                    child.ptr,
                    aux.db_metadata()->at(child.offset.id)->insertion_count());
                // free node if path longer than CACHE_LEVEL
                // do not free if n == 1, that's when parent is a leaf node
                // with branches
                auto cache_opt = sm.cache_option();
                if (number_of_children > 1 && prefix_index > 0 &&
                    (cache_opt == CacheOption::DisposeAll ||
                     (cache_opt == CacheOption::ApplyLevelBasedCache &&
                      (prefix_index + 1 + child.ptr->path_nibbles_len() >
                       CACHE_LEVEL)))) {
                    {
                        Node::UniquePtr const _{child.ptr};
                    }
                    child.ptr = nullptr;
                }
            }
        }
    }
    return create_node(sm.get_compute(), mask, children, path, leaf_data);
}

void create_node_compute_data_possibly_async(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, tnode_unique_ptr tnode, bool const might_on_disk)
{
    if (might_on_disk && tnode->number_of_children() == 1) {
        auto &child = tnode->children[bitmask_index(
            tnode->orig_mask,
            static_cast<unsigned>(std::countr_zero(tnode->mask)))];
        if (!child.ptr) {
            MONAD_DEBUG_ASSERT(aux.is_on_disk());
            read_single_child_receiver receiver(
                &aux, sm.clone(), tnode.release(), child);
            async_read(aux, std::move(receiver));
            MONAD_DEBUG_ASSERT(parent.npending);
            return;
        }
    }
    Node *node = create_node_from_children_if_any(
        aux,
        sm,
        tnode->orig_mask,
        tnode->mask,
        tnode->children,
        tnode->prefix_index,
        tnode->path,
        tnode->opt_leaf_data);
    MONAD_DEBUG_ASSERT(entry.branch < 16);
    MONAD_DEBUG_ASSERT(entry.parent_trie_section != uint8_t(-1));
    if (node) {
        entry.set_node_and_compute_data(node, sm);
    }
    else {
        parent.mask &=
            static_cast<uint16_t>(~(static_cast<uint16_t>(1u << entry.branch)));
        entry.erase();
    }
    --parent.npending;
}

// update leaf data of old, old can have branches
void update_leaf_data_(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, Node::UniquePtr old, NibblesView const path,
    Update &update)
{
    if (update.is_deletion()) {
        parent.mask &= static_cast<uint16_t>(~(1u << entry.branch));
        entry.erase();
        MONAD_DEBUG_ASSERT(entry.ptr == nullptr);
        --parent.npending;
        return;
    }
    if (!update.next.empty()) {
        sm.forward();
        Requests requests;
        requests.split_into_sublists(std::move(update.next), 0);
        MONAD_ASSERT(requests.opt_leaf == std::nullopt);
        if (update.incarnation) {
            create_new_trie_from_requests_(
                aux, sm, entry, requests, path, 0, update.value);
            --parent.npending;
        }
        else {
            auto const opt_leaf =
                update.value.has_value() ? update.value : old->opt_value();
            dispatch_updates_impl_(
                aux,
                sm,
                parent,
                entry,
                std::move(old),
                requests,
                0,
                path,
                opt_leaf);
        }
        sm.backward();
        return;
    }
    // only value update but not subtrie updates
    MONAD_ASSERT(update.value.has_value());
    // TODO if not incarnation, should check whether children need compaction;
    Node *node =
        update.incarnation
            ? make_node(0, {}, path, update.value.value(), {}).release()
            : make_node(*old.get(), path, update.value).release();
    MONAD_ASSERT(node);
    entry.set_node_and_compute_data(node, sm);
    --parent.npending;
}

/////////////////////////////////////////////////////
// Create a new trie from a list of updates, no incarnation
/////////////////////////////////////////////////////
void create_new_trie_(
    UpdateAux &aux, TrieStateMachine &sm, ChildData &entry,
    UpdateList &&updates, unsigned prefix_index)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &update = updates.front();
        MONAD_DEBUG_ASSERT(update.value.has_value());
        auto const path = update.key.substr(prefix_index);
        if (!update.next.empty()) { // nested updates
            sm.forward();
            Requests requests;
            requests.split_into_sublists(std::move(update.next), 0);
            MONAD_DEBUG_ASSERT(update.value.has_value());
            MONAD_DEBUG_ASSERT(entry.parent_trie_section != uint8_t(-1));
            create_new_trie_from_requests_(
                aux, sm, entry, requests, path, 0, update.value);
            sm.backward();
            return;
        }
        entry.set_node_and_compute_data(
            make_node(0, {}, path, update.value.value(), {}).release(), sm);
        return;
    }
    Requests requests;
    auto const prefix_index_start = prefix_index;
    while (requests.split_into_sublists(
               std::move(updates), prefix_index) == // NOLINT
               1 &&
           !requests.opt_leaf) {
        updates = std::move(requests).first_and_only_list();
        ++prefix_index;
    }
    create_new_trie_from_requests_(
        aux,
        sm,
        entry,
        requests,
        requests.get_first_path().substr(
            prefix_index_start, prefix_index - prefix_index_start),
        prefix_index,
        requests.opt_leaf.and_then(&Update::value));
}

void create_new_trie_from_requests_(
    UpdateAux &aux, TrieStateMachine &sm, ChildData &entry, Requests &requests,
    NibblesView const path, unsigned const prefix_index,
    std::optional<byte_string_view> const opt_leaf_data)
{
    auto const number_of_children =
        static_cast<unsigned>(std::popcount(requests.mask));
    uint16_t const mask = requests.mask;
    allocators::owning_span<ChildData> children(number_of_children);
    for (unsigned i = 0, j = 0, bit = 1; j < number_of_children;
         ++i, bit <<= 1) {
        if (bit & requests.mask) {
            children[j].set_branch_and_section(i, sm.get_state());
            create_new_trie_(
                aux, sm, children[j], std::move(requests)[i], prefix_index + 1);
            ++j;
        }
    }
    entry.set_node_and_compute_data(
        create_node_from_children_if_any(
            aux, sm, mask, mask, children, prefix_index, path, opt_leaf_data),
        sm);
}

/////////////////////////////////////////////////////
// Update existing subtrie
/////////////////////////////////////////////////////

void upsert_(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, Node::UniquePtr old, UpdateList &&updates,
    unsigned prefix_index, unsigned old_prefix_index)
{
    MONAD_DEBUG_ASSERT(old);
    MONAD_DEBUG_ASSERT(old_prefix_index != INVALID_PATH_INDEX);
    unsigned const old_prefix_index_start = old_prefix_index;
    Requests requests;
    while (true) {
        NibblesView path{
            old_prefix_index_start, old_prefix_index, old->path_data()};
        if (updates.size() == 1 &&
            prefix_index == updates.front().key.nibble_size()) {
            auto &update = updates.front();
            update_leaf_data_(
                aux, sm, parent, entry, std::move(old), path, update);
            return;
        }
        unsigned const number_of_sublists = requests.split_into_sublists(
            std::move(updates), prefix_index); // NOLINT
        MONAD_DEBUG_ASSERT(number_of_sublists);
        if (old_prefix_index == old->path_nibble_index_end) {
            dispatch_updates_flat_list_(
                aux,
                sm,
                parent,
                entry,
                std::move(old),
                requests,
                path,
                prefix_index);
            return;
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_prefix_index);
            number_of_sublists == 1 &&
            requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++prefix_index;
            ++old_prefix_index;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        mismatch_handler_(
            aux,
            sm,
            parent,
            entry,
            std::move(old),
            requests,
            path,
            old_prefix_index,
            prefix_index);
        return;
    }
}

void fillin_entry(
    UpdateAux &aux, TrieStateMachine &sm, tnode_unique_ptr tnode,
    UpwardTreeNode &parent, ChildData &entry)
{
    if (tnode->npending) {
        tnode.release();
    }
    else {
        create_node_compute_data_possibly_async(
            aux, sm, parent, entry, std::move(tnode));
    }
}

/* dispatch updates at the end of old node's path. old node may have leaf data,
 * and there might be update to the leaf value. */
void dispatch_updates_impl_(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, Node::UniquePtr old_ptr, Requests &requests,
    unsigned const prefix_index, NibblesView const path,
    std::optional<byte_string_view> const opt_leaf_data)
{
    Node *old = old_ptr.get();
    uint16_t const orig_mask = old->mask | requests.mask;
    auto const number_of_children =
        static_cast<unsigned>(std::popcount(orig_mask));
    auto tnode = make_tnode(
        orig_mask,
        prefix_index,
        sm.get_state(),
        &parent,
        entry.branch,
        path,
        opt_leaf_data,
        opt_leaf_data.has_value() ? std::move(old_ptr) : Node::UniquePtr{});
    MONAD_DEBUG_ASSERT(
        tnode->children.size() == number_of_children && number_of_children > 0);
    auto &children = tnode->children;

    for (unsigned i = 0, j = 0, bit = 1; j < number_of_children;
         ++i, bit <<= 1) {
        MONAD_DEBUG_ASSERT(i <= std::numeric_limits<uint8_t>::max());
        if (bit & requests.mask) {
            children[j].set_branch_and_section(i, sm.get_state());
            if (bit & old->mask) {
                Node::UniquePtr next_ = old->next_ptr(old->to_child_index(i));
                if (!next_) {
                    update_receiver receiver(
                        &aux,
                        sm.clone(),
                        children[j],
                        old->fnext(old->to_child_index(i)),
                        std::move(requests)[i],
                        tnode.get(),
                        prefix_index + 1);
                    async_read(aux, std::move(receiver));
                    ++j;
                    continue;
                }
                unsigned const next_prefix_index = next_->path_start_nibble();
                upsert_(
                    aux,
                    sm,
                    *tnode,
                    children[j],
                    std::move(next_),
                    std::move(requests)[i],
                    prefix_index + 1,
                    next_prefix_index);
            }
            else {
                create_new_trie_(
                    aux,
                    sm,
                    children[j],
                    std::move(requests)[i],
                    prefix_index + 1);
                --tnode->npending;
            }
            ++j;
        }
        else if (bit & old->mask) {
            children[j].copy_old_child(old, i);
            --tnode->npending;
            ++j;
        }
    }
    fillin_entry(aux, sm, std::move(tnode), parent, entry);
}

void dispatch_updates_flat_list_(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, Node::UniquePtr old, Requests &requests,
    NibblesView const path, unsigned prefix_index)
{
    auto &opt_leaf = requests.opt_leaf;
    auto opt_leaf_data = old->opt_value();
    if (opt_leaf.has_value()) {
        sm.forward();
        MONAD_ASSERT(opt_leaf->next.empty());
        if (opt_leaf.value().incarnation) {
            // incarnation means there are new children keys longer than
            // curr update's key
            MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
            create_new_trie_from_requests_(
                aux,
                sm,
                entry,
                requests,
                path,
                prefix_index,
                opt_leaf.value().value);
            --parent.npending;
            return;
        }
        else if (opt_leaf.value().is_deletion()) {
            parent.mask &= static_cast<uint16_t>(~(1u << entry.branch));
            entry.erase();
            --parent.npending;
            return;
        }
        if (opt_leaf.value().value.has_value()) {
            opt_leaf_data = opt_leaf.value().value;
        }
    }
    dispatch_updates_impl_(
        aux,
        sm,
        parent,
        entry,
        std::move(old),
        requests,
        prefix_index,
        path,
        opt_leaf_data);
    if (opt_leaf.has_value()) {
        sm.backward();
    }
}

// Split `old` at old_prefix_index, `updates` are already splitted at
// prefix_index to `requests`, which can have 1 or more sublists.
void mismatch_handler_(
    UpdateAux &aux, TrieStateMachine &sm, UpwardTreeNode &parent,
    ChildData &entry, Node::UniquePtr old_ptr, Requests &requests,
    NibblesView const path, unsigned const old_prefix_index,
    unsigned const prefix_index)
{
    Node &old = *old_ptr;
    MONAD_DEBUG_ASSERT(old.has_path());
    // Note: no leaf can be created at an existing non-leaf node
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char const old_nibble =
        get_nibble(old.path_data(), old_prefix_index);
    uint16_t const orig_mask =
        static_cast<uint16_t>(1u << old_nibble | requests.mask);
    auto tnode = make_tnode(
        orig_mask, prefix_index, sm.get_state(), &parent, entry.branch, path);
    auto const number_of_children =
        static_cast<unsigned>(std::popcount(orig_mask));
    MONAD_DEBUG_ASSERT(
        tnode->children.size() == number_of_children && number_of_children > 0);
    auto &children = tnode->children;

    for (unsigned i = 0, j = 0, bit = 1; j < number_of_children;
         ++i, bit <<= 1) {
        if (bit & requests.mask) {
            children[j].set_branch_and_section(i, sm.get_state());
            if (i == old_nibble) {
                upsert_(
                    aux,
                    sm,
                    *tnode,
                    children[j],
                    std::move(old_ptr),
                    std::move(requests)[i],
                    prefix_index + 1,
                    old_prefix_index + 1);
            }
            else {
                create_new_trie_(
                    aux,
                    sm,
                    children[j],
                    std::move(requests)[i],
                    prefix_index + 1);
                --tnode->npending;
            }
            ++j;
        }
        else if (i == old_nibble) {
            // nexts[j] is a path-shortened old node, trim prefix
            NibblesView const path_suffix{
                old_prefix_index + 1,
                old.path_nibble_index_end,
                old.path_data()};
            children[j].set_branch_and_section(i, sm.get_state());
            children[j].set_node_and_compute_data(
                make_node(old, path_suffix, old.opt_value()).release(), sm);
            --tnode->npending;
            ++j;
        }
    }
    fillin_entry(aux, sm, std::move(tnode), parent, entry);
}

/////////////////////////////////////////////////////
// Async write
/////////////////////////////////////////////////////
node_writer_unique_ptr_type replace_node_writer(
    UpdateAux &aux, node_writer_unique_ptr_type &node_writer,
    size_t bytes_yet_to_be_appended_to_existing = 0)
{
    // Can't use add_to_offset(), because it asserts if we go past the
    // capacity
    auto offset_of_next_block = node_writer->sender().offset();
    bool in_fast_list =
        aux.db_metadata()->at(offset_of_next_block.id)->in_fast_list;
    file_offset_t offset = offset_of_next_block.offset;
    offset += node_writer->sender().written_buffer_bytes() +
              bytes_yet_to_be_appended_to_existing;
    offset_of_next_block.offset = offset & chunk_offset_t::max_offset;
    auto block_size = AsyncIO::WRITE_BUFFER_SIZE;
    auto const chunk_capacity = aux.io->chunk_capacity(offset_of_next_block.id);
    MONAD_ASSERT(offset <= chunk_capacity);
    if (offset == chunk_capacity) {
        auto const *ci_ = aux.db_metadata()->free_list_begin();
        MONAD_ASSERT(ci_ != nullptr); // we are out of free blocks!
        auto idx = ci_->index(aux.db_metadata());
        aux.remove(idx);
        aux.append(
            in_fast_list ? UpdateAux::chunk_list::fast
                         : UpdateAux::chunk_list::slow,
            idx);
        // TODO assert that count is incremented by one
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

async_write_node_result async_write_node(
    UpdateAux &aux, node_writer_unique_ptr_type &node_writer, Node const &node)
{
    aux.io->poll_nonblocking_if_not_within_completions(1);
    auto *sender = &node_writer->sender();
    auto const size = node.disk_size;
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
        auto new_node_writer =
            replace_node_writer(aux, node_writer, remaining_bytes);
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

chunk_offset_t
async_write_node_set_spare(UpdateAux &aux, Node &node, bool write_to_fast)
{
    if (aux.alternate_slow_fast_writer) {
        // alternate between slow and fast writer
        write_to_fast &= aux.can_write_to_fast;
        aux.can_write_to_fast = !aux.can_write_to_fast;
    }
    auto off = async_write_node(
                   aux,
                   write_to_fast ? aux.node_writer_fast : aux.node_writer_slow,
                   node)
                   .offset_written_to;
    auto const pages = num_pages(off.offset, node.get_disk_size());
    MONAD_DEBUG_ASSERT(pages <= std::numeric_limits<uint16_t>::max());
    off.spare = static_cast<uint16_t>(pages);
    return off;
}

async_write_node_result write_new_root_node(UpdateAux &aux, Node const &root)
{
    auto const ret = async_write_node(aux, aux.node_writer_fast, root);
    // Round up with all bits zero
    auto replace = [&](node_writer_unique_ptr_type &node_writer) {
        auto *sender = &node_writer->sender();
        auto written = sender->written_buffer_bytes();
        auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
        auto const tozerobytes = paddedup - written;
        auto *tozero = sender->advance_buffer_append(tozerobytes);
        assert(tozero != nullptr);
        memset(tozero, 0, tozerobytes);
        // replace fast node writer
        auto new_node_writer = replace_node_writer(aux, node_writer);
        auto to_initiate = std::move(node_writer);
        node_writer = std::move(new_node_writer);
        to_initiate->initiate();
        // shall be recycled by the i/o receiver
        to_initiate.release();
    };
    replace(aux.node_writer_fast);
    if (aux.node_writer_slow->sender().written_buffer_bytes()) {
        // replace slow node writer
        replace(aux.node_writer_slow);
    }
    // flush async write root and slow writer
    aux.io->flush();
    // write new root offset and slow ring's latest offset to the front of disk
    aux.advance_offsets_to(
        ret.offset_written_to, aux.node_writer_slow->sender().offset());
    return ret;
}

MONAD_MPT_NAMESPACE_END
