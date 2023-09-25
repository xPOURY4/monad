#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

using namespace MONAD_ASYNC_NAMESPACE;

/* Names: `pi` is nibble index in prefix of an update,
 `old_pi` is nibble index of relpath in previous node - old.
 `*psi` is the starting nibble index in current function frame
*/
node_ptr _dispatch_updates(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, Requests &requests, unsigned pi,
    NibblesView const relpath);

node_ptr _mismatch_handler(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, Requests &requests, unsigned const old_psi,
    unsigned const old_pi, unsigned const pi);

node_ptr _create_new_trie(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    UpdateList &&updates, unsigned pi = 0);

node_ptr _create_new_trie_from_requests(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Requests &requests, NibblesView const relpath, unsigned const pi,
    std::optional<byte_string_view> const opt_leaf_data);

node_ptr _upsert(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, UpdateList &&updates, unsigned pi = 0,
    unsigned old_pi = 0);

node_ptr upsert(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, UpdateList &&updates)
{
    node_ptr node =
        old ? _upsert(comp, io, node_writer, old, std::move(updates))
            : _create_new_trie(comp, io, node_writer, std::move(updates));
    return node;
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
node_ptr _update_leaf_data(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, NibblesView const relpath, Update const u)
{
    if (u.is_deletion()) {
        return nullptr;
    }
    if (u.next) {
        Requests requests;
        requests.split_into_sublists(std::move(*(UpdateList *)u.next), 0);
        return u.incarnation
                   ? _create_new_trie_from_requests(
                         comp,
                         io,
                         node_writer,
                         requests,
                         relpath,
                         0,
                         _get_leaf_data(u))
                   : _dispatch_updates(
                         comp, io, node_writer, old, requests, 0, relpath);
    }
    // create new leaf, without children upserts
    if (u.incarnation) {
        return create_leaf(u.opt.value().data(), relpath);
    }
    // keep old's child if any
    return update_node_shorter_path(
        old, relpath, _get_leaf_data(u, old->opt_leaf()));
}

node_ptr _upsert(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, UpdateList &&updates, unsigned pi, unsigned old_pi)
{
    MONAD_DEBUG_ASSERT(old);
    unsigned const old_psi = old_pi;
    Requests requests;
    while (true) {
        NibblesView relpath{old_psi, old_pi, old->path_data()};
        if (updates.size() == 1 && pi == updates.front().key.size() * 2) {
            return _update_leaf_data(
                comp, io, node_writer, old, relpath, updates.front());
        }
        unsigned const n = requests.split_into_sublists(std::move(updates), pi);
        MONAD_DEBUG_ASSERT(n);
        if (old_pi == old->path_nibble_index_end) {
            return _dispatch_updates(
                comp, io, node_writer, old, requests, pi, relpath);
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_pi);
            n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++pi;
            ++old_pi;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        return _mismatch_handler(
            comp, io, node_writer, old, requests, old_psi, old_pi, pi);
    }
}

// create a new trie from a list of updates, won't have incarnation
node_ptr _create_new_trie(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    UpdateList &&updates, unsigned pi)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &u = updates.front();
        MONAD_DEBUG_ASSERT(u.incarnation == false && u.opt.has_value());
        NibblesView const relpath{
            pi, (uint8_t)(2 * u.key.size()), u.key.data()};
        if (u.next) {
            Requests requests;
            requests.split_into_sublists(std::move(*(UpdateList *)u.next), 0);
            MONAD_DEBUG_ASSERT(u.opt.has_value());
            return _create_new_trie_from_requests(
                comp, io, node_writer, requests, relpath, 0, _get_leaf_data(u));
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
        comp,
        io,
        node_writer,
        requests,
        NibblesView{psi, pi, requests.get_first_path()},
        pi,
        _get_leaf_data(requests.opt_leaf));
}

void write_node_and_compute_hash(
    ChildData &dest, Compute &comp, AsyncIO &io,
    node_writer_unique_ptr_type &node_writer, Node *const node, uint8_t const i)
{
    dest.ptr = node;
    dest.offset = async_write_node(io, node_writer, node).offset_written_to;
    dest.branch = i;
    dest.len = comp.compute(dest.data, node);
};

node_ptr _create_new_trie_from_requests(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Requests &requests, NibblesView const relpath, unsigned const pi,
    std::optional<byte_string_view> const opt_leaf_data)
{
    unsigned const n = bitmask_count(requests.mask);
    uint16_t const mask = requests.mask;
    MONAD_DEBUG_ASSERT(n > 0);
    ChildData children[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            auto const node =
                _create_new_trie(
                    comp, io, node_writer, std::move(requests)[i], pi + 1)
                    .release();
            write_node_and_compute_hash(
                children[j++], comp, io, node_writer, node, i);
        }
    }
    return create_node(comp, mask, mask, {children, n}, relpath, opt_leaf_data);
}

//! dispatch updates at the end of old node's path
//! old node can have leaf data, there might be update to that leaf
//! return a new node
node_ptr _dispatch_updates(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, Requests &requests, unsigned pi, NibblesView const relpath)
{
    uint16_t mask = old->mask | requests.mask;
    uint16_t const orig = mask;
    unsigned const n = bitmask_count(mask);

    auto const &opt_leaf = requests.opt_leaf;
    if (opt_leaf.has_value() && opt_leaf.value().incarnation) {
        // incranation = 1, also have new children longer than curr update's key
        MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
        return _create_new_trie_from_requests(
            comp,
            io,
            node_writer,
            requests,
            relpath,
            pi,
            _get_leaf_data(opt_leaf));
    }
    ChildData children[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            auto const node =
                (bit & old->mask)
                    ? [&] {
                          node_ptr next_ = old->next_ptr(i);
                          auto res = _upsert(
                              comp,
                              io,
                              node_writer,
                              next_.get(),
                              std::move(requests)[i],
                              pi + 1,
                              next_->bitpacked.path_nibble_index_start).release();
                          return res;
                      }()
                    : _create_new_trie(comp, io, node_writer, std::move(requests)[i], pi + 1).release();
            if (node) {
                write_node_and_compute_hash(
                    children[j], comp, io, node_writer, node, i);
            }
            else {
                mask &= ~bit;
            }
            ++j;
        }
        else if (bit & old->mask) {
            children[j].ptr = old->next(i);
            old->next(i) = nullptr;
            auto const data = old->child_data_view(i);
            memcpy(&children[j].data, data.data(), data.size());
            children[j].len = data.size();
            children[j].branch = i;
            children[j].offset = old->fnext(i);
            ++j;
        }
    }
    // no incarnation and no erase at this point
    auto const opt_leaf_data = _get_leaf_data(opt_leaf, old->opt_leaf());
    return create_node(comp, orig, mask, {children, n}, relpath, opt_leaf_data);
}

//! split old at old_pi, updates at pi
//! requests can have 1 or more sublists
node_ptr _mismatch_handler(
    Compute &comp, AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    Node *const old, Requests &requests, unsigned const old_psi,
    unsigned const old_pi, unsigned const pi)
{
    MONAD_DEBUG_ASSERT(old->has_relpath());
    // Note: no leaf can be created at an existing non-leaf node
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char const old_nibble = get_nibble(old->path_data(), old_pi);
    uint16_t mask = 1u << old_nibble | requests.mask;
    uint16_t const orig = mask;
    unsigned const n = bitmask_count(mask);
    MONAD_DEBUG_ASSERT(n > 1);
    ChildData children[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            auto const node =
                (i == old_nibble)
                    ? _upsert(
                          comp,
                          io,
                          node_writer,
                          old,
                          std::move(requests)[i],
                          pi + 1,
                          old_pi + 1)
                          .release()
                    : _create_new_trie(
                          comp, io, node_writer, std::move(requests)[i], pi + 1)
                          .release();
            if (node) {
                write_node_and_compute_hash(
                    children[j], comp, io, node_writer, node, i);
            }
            else {
                mask &= ~bit;
            }
            ++j;
        }
        else if (i == old_nibble) {
            // nexts[j] is a path-shortened old node, trim prefix
            NibblesView relpath{
                old_pi + 1, old->path_nibble_index_end, old->path_data()};
            write_node_and_compute_hash(
                children[j++],
                comp,
                io,
                node_writer,
                update_node_shorter_path(old, relpath, old->opt_leaf())
                    .release(),
                i);
        }
    }
    // trim suffix
    return create_node(
        comp,
        orig,
        mask,
        {children, n},
        NibblesView{old_psi, old_pi, old->path_data()});
}

node_writer_unique_ptr_type replace_node_writer(
    AsyncIO &io, node_writer_unique_ptr_type &node_writer,
    size_t bytes_yet_to_be_appended_to_existing = 0)
{
    auto ret = io.make_connected(
        write_single_buffer_sender{
            node_writer->sender().offset() +
                node_writer->sender().written_buffer_bytes() +
                bytes_yet_to_be_appended_to_existing,
            {(const std::byte *)nullptr, AsyncIO::WRITE_BUFFER_SIZE}},
        write_operation_io_receiver{});
    return ret;
}

async_write_node_result async_write_node(
    AsyncIO &io, node_writer_unique_ptr_type &node_writer, Node *node)
{
    io.poll_nonblocking(1);
    auto *sender = &node_writer->sender();
    const auto size = node->get_disk_size();
    const async_write_node_result ret{
        sender->offset() + sender->written_buffer_bytes(),
        size,
        node_writer.get()};
    const auto remaining_bytes = sender->remaining_buffer_bytes();
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

// async_write_node_result flush_and_write_new_root_node(
//     AsyncIO &io, node_writer_unique_ptr_type
//     node_writer, Node *root)
// {
//     io.flush();
//     if (!root->mask) {
//         return {INVALID_OFFSET, 0, nullptr};
//     }
//     auto ret = async_write_node(root);
//     // Round up with all bits zero
//     auto *sender = &node_writer->sender();
//     auto written = sender->written_buffer_bytes();
//     auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
//     const auto tozerobytes = paddedup - written;
//     auto *tozero = sender->advance_buffer_append(tozerobytes);
//     assert(tozero != nullptr);
//     memset(tozero, 0, tozerobytes);
//     auto to_initiate = replace_node_writer();
//     to_initiate->initiate();
//     // shall be recycled by the i/o receiver
//     to_initiate.release();
//     return ret;
// }

MONAD_MPT_NAMESPACE_END