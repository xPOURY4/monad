#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

/* Names: `pi` is nibble index in prefix of an update,
 `old_pi` is nibble index of relpath in previous node - old.
 `*psi` is the starting nibble index in current function frame
*/
node_ptr _dispatch_updates(
    Compute &comp, Node *const old, Requests &requests, unsigned pi,
    NibblesView const relpath);

node_ptr _mismatch_handler(
    Compute &comp, Node *const old, Requests &requests, unsigned const old_psi,
    unsigned const old_pi, unsigned const pi);

node_ptr _create_new_trie(Compute &comp, UpdateList &&updates, unsigned pi = 0);

node_ptr _create_new_trie_from_requests(
    Compute &comp, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data);

node_ptr _upsert(
    Compute &comp, Node *const old, UpdateList &&updates, unsigned pi = 0,
    unsigned old_pi = 0);

node_ptr upsert(Compute &comp, Node *const old, UpdateList &&updates)
{
    node_ptr node = old ? _upsert(comp, old, std::move(updates))
                        : _create_new_trie(comp, std::move(updates));
    return node;
}

//! get leaf data from new update and old leaf, whose default is {}

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
    Compute &comp, Node *const old, NibblesView const relpath, Update const &u)
{
    if (u.is_deletion()) {
        return nullptr;
    }
    if (u.next) {
        Requests requests;
        requests.split_into_sublists(std::move(*(UpdateList *)u.next), 0);
        return u.incarnation
                   ? _create_new_trie_from_requests(
                         comp, requests, relpath, 0, _get_leaf_data(u))
                   : _dispatch_updates(comp, old, requests, 0, relpath);
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
    Compute &comp, Node *const old, UpdateList &&updates, unsigned pi,
    unsigned old_pi)
{
    MONAD_DEBUG_ASSERT(old);
    unsigned const old_psi = old_pi;
    Requests requests;
    while (true) {
        NibblesView relpath{old_psi, old_pi, old->path_data()};
        if (updates.size() == 1 && pi == updates.front().key.size() * 2) {
            return _update_leaf_data(comp, old, relpath, updates.front());
        }
        unsigned const n = requests.split_into_sublists(std::move(updates), pi);
        MONAD_DEBUG_ASSERT(n);
        if (old_pi == old->path_nibble_index_end) {
            return _dispatch_updates(comp, old, requests, pi, relpath);
        }
        if (auto old_nibble = get_nibble(old->path_data(), old_pi);
            n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++pi;
            ++old_pi;
            continue;
        }
        // meet a mismatch or split, not till the end of old path
        return _mismatch_handler(comp, old, requests, old_psi, old_pi, pi);
    }
}

// create a new trie from a list of updates, won't have incarnation
node_ptr _create_new_trie(Compute &comp, UpdateList &&updates, unsigned pi)
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
                comp, requests, relpath, 0, _get_leaf_data(u));
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
        requests,
        NibblesView{psi, pi, requests.get_first_path()},
        pi,
        _get_leaf_data(requests.opt_leaf));
}

node_ptr _create_new_trie_from_requests(
    Compute &comp, Requests &requests, NibblesView const relpath,
    unsigned const pi, std::optional<byte_string_view> const opt_leaf_data)
{
    unsigned const n = bitmask_count(requests.mask);
    uint16_t const mask = requests.mask;
    MONAD_DEBUG_ASSERT(n > 0);
    ChildData hashes[n];
    node_ptr nexts[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            nexts[j] = _create_new_trie(comp, std::move(requests)[i], pi + 1);
            hashes[j].len = comp.compute(hashes[j].data, nexts[j].get());
            hashes[j].branch = i;
            ++j;
        }
    }
    return create_node(
        comp, mask, mask, {hashes, n}, {nexts, n}, relpath, opt_leaf_data);
}

//! dispatch updates at the end of old node's path
//! old node can have leaf data, there might be update to that leaf
//! return a new node
node_ptr _dispatch_updates(
    Compute &comp, Node *const old, Requests &requests, unsigned pi,
    NibblesView const relpath)
{
    uint16_t mask = old->mask | requests.mask;
    uint16_t const orig = mask;
    unsigned const n = bitmask_count(mask);

    auto const &opt_leaf = requests.opt_leaf;
    if (opt_leaf.has_value() && opt_leaf.value().incarnation) {
        // incranation = 1, also have new children longer than curr update's key
        MONAD_DEBUG_ASSERT(!opt_leaf.value().is_deletion());
        return _create_new_trie_from_requests(
            comp, requests, relpath, pi, _get_leaf_data(opt_leaf));
    }
    ChildData hashes[n];
    node_ptr nexts[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            nexts[j] =
                (bit & old->mask)
                    ? _upsert(
                          comp,
                          old->next(i),
                          std::move(requests)[i],
                          pi + 1,
                          old->next(i)->path_nibble_index_start)
                    : _create_new_trie(comp, std::move(requests)[i], pi + 1);
            if (!nexts[j]) {
                mask &= ~bit;
            }
            else {
                hashes[j].len = comp.compute(hashes[j].data, nexts[j].get());
                hashes[j].branch = i;
            }
            ++j;
        }
        else if (bit & old->mask) {
            nexts[j] = old->next_ptr(i);
            auto const data = old->child_data_view(i);
            memcpy(&hashes[j].data, data.data(), data.size());
            hashes[j].len = data.size();
            hashes[j].branch = i;
            ++j;
        }
    }
    // no incarnation and no erase at this point
    auto const opt_leaf_data = _get_leaf_data(opt_leaf, old->opt_leaf());
    return create_node(
        comp, orig, mask, {hashes, n}, {nexts, n}, relpath, opt_leaf_data);
}

//! split old at old_pi, updates at pi
//! requests can have 1 or more sublists
node_ptr _mismatch_handler(
    Compute &comp, Node *const old, Requests &requests, unsigned const old_psi,
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
    ChildData hashes[n];
    node_ptr nexts[n];
    for (unsigned i = 0, j = 0, bit = 1; j < n; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            nexts[j] =
                i == old_nibble
                    ? _upsert(
                          comp, old, std::move(requests)[i], pi + 1, old_pi + 1)
                    : _create_new_trie(comp, std::move(requests)[i], pi + 1);
            if (!nexts[j]) {
                mask &= ~bit;
            }
            else {
                hashes[j].len = comp.compute(hashes[j].data, nexts[j].get());
                hashes[j].branch = i;
            }
            ++j;
        }
        else if (i == old_nibble) {
            // nexts[j] is a path-shortened old node, trim prefix
            NibblesView relpath{
                old_pi + 1, old->path_nibble_index_end, old->path_data()};
            nexts[j] = update_node_shorter_path(old, relpath, old->opt_leaf());
            hashes[j].branch = i;
            hashes[j].len = comp.compute(hashes[j].data, nexts[j].get());
            ++j;
        }
    }
    // trim suffix
    return create_node(
        comp,
        orig,
        mask,
        {hashes, n},
        {nexts, n},
        NibblesView{old_psi, old_pi, old->path_data()});
}

MONAD_MPT_NAMESPACE_END