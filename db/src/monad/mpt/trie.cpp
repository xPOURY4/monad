#include <monad/mpt/compute.hpp>
#include <monad/mpt/nibbles_view.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/request.hpp>
#include <monad/mpt/trie.hpp>

#include <cstdint>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

/* Names: `pi` is nibble index in prefix of an update,
 `old_pi` is nibble index of relpath in previous node - old.
 `*psi` is the starting nibble index in current function frame
*/
Node *_dispatch_updates(
    Compute &comp, Node *const old, Requests &requests, unsigned pi,
    NibblesView &relpath);

Node *_mismatch_handler(
    Compute &comp, Node *const old, Requests &requests, unsigned const old_psi,
    unsigned const old_pi, unsigned const pi);

Node *_create_new_trie(Compute &comp, UpdateList &&updates, unsigned pi = 0);

Node *_upsert(
    Compute &comp, Node *const old, UpdateList &&updates, unsigned pi = 0,
    unsigned old_pi = 0);

Node *upsert(Compute &comp, Node *const old, UpdateList &&updates)
{
    Node *node =
        (old ? _upsert(comp, old, std::move(updates))
             : _create_new_trie(comp, std::move(updates)));
    free_trie(old);
    return node;
}

//! get leaf data from new update and old leaf, whose default is {}
byte_string_view
_get_leaf_data(std::optional<Update> opt_leaf, byte_string_view old_leaf)
{
    if (opt_leaf.has_value()) {
        return opt_leaf.value().opt.value();
    }
    return old_leaf; // can be 0-length {}
}

//! update leaf data of old, old can have branches
Node *_update_leaf_data(Node *const old, NibblesView relpath, Update &update)
{
    byte_string_view leaf_data = _get_leaf_data(update, old->leaf_view());
    // create node with new leaf data
    return create_node_from_prev(old, relpath, leaf_data);
}

Node *_upsert(
    Compute &comp, Node *const old, UpdateList &&updates, unsigned pi,
    unsigned old_pi)
{
    MONAD_DEBUG_ASSERT(old);
    unsigned const old_psi = old_pi;
    Requests requests;
    while (true) {
        if (updates.size() == 1 && pi == updates.front().key.size() * 2) {
            // old leaf may have children, but only update leaf data here
            NibblesView relpath{old_psi, old_pi, old->path_data()};
            return _update_leaf_data(old, relpath, updates.front());
        }
        unsigned n = requests.split_into_sublists(std::move(updates), pi);
        MONAD_DEBUG_ASSERT(n);
        // end of old path
        if (old_pi == old->path_ei) {
            NibblesView relpath{old_psi, old_pi, old->path_data()};
            return _dispatch_updates(comp, old, requests, pi, relpath);
        }
        // old path hasn't end, but split or mismatch
        unsigned char old_nibble = get_nibble(old->path_data(), old_pi);
        if (n == 1 && requests.get_first_branch() == old_nibble) {
            updates = std::move(requests)[old_nibble];
            ++pi;
            ++old_pi;
        }
        else {
            return _mismatch_handler(comp, old, requests, old_psi, old_pi, pi);
        }
    }
}

Node *_create_new_trie(Compute &comp, UpdateList &&updates, unsigned pi)
{
    MONAD_DEBUG_ASSERT(updates.size());
    if (updates.size() == 1) {
        Update &u = updates.front();
        NibblesView relpath{pi, (uint8_t)(2 * u.key.size()), u.key.data()};
        return create_leaf(u.opt.value(), relpath);
    }
    Requests requests;
    uint8_t psi = pi;
    unsigned n;
    while (true) {
        n = requests.split_into_sublists(std::move(updates), pi);
        if (n > 1 || requests.opt_leaf.has_value()) {
            break;
        }
        updates = std::move(requests)[requests.get_first_branch()];
        ++pi;
    }
    ChildData hashes[n];
    Node *nexts[n];
    unsigned char const *first_path =
        requests[requests.get_first_branch()].front().key.data();
    for (unsigned i = 0, j = 0, bit = 1; i < 16; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            nexts[j] = _create_new_trie(comp, std::move(requests)[i], pi + 1);
            // special case: n == 1, comp.compute() with relpath prefix
            hashes[j].len = comp.compute(
                hashes[j].data, nexts[j], (n == 1 ? i : (unsigned)-1));
            ++j;
        }
    }
    // compute hash length
    NibblesView relpath{psi, pi, first_path};
    byte_string_view leaf_data = _get_leaf_data(requests.opt_leaf, {});
    return create_node(
        comp,
        requests.mask,
        std::span<ChildData>(hashes, n),
        std::span<Node *>(nexts, n),
        relpath,
        leaf_data);
}

//! dispatch updates at the end of old node's path
//! old node can have leaf data, there might be update to that leaf
//! return a new node
Node *_dispatch_updates(
    Compute &comp, Node *const old, Requests &requests, unsigned pi,
    NibblesView &relpath)
{
    uint16_t mask = old->mask | requests.mask;
    unsigned n = bitmask_count(mask);

    ChildData hashes[n];
    Node *nexts[n];
    for (unsigned i = 0, j = 0, bit = 1; i < 16; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            if (bit & old->mask) {
                Node *next = old->next(i);
                nexts[j] = _upsert(
                    comp, next, std::move(requests)[i], pi + 1, next->path_si);
            }
            else {
                nexts[j] =
                    _create_new_trie(comp, std::move(requests)[i], pi + 1);
            }
            // special case: n == 1, comp.compute() with relpath prefix
            hashes[j].len = comp.compute(
                hashes[j].data, nexts[j], (n == 1 ? i : (unsigned)-1));
            ++j;
        }
        else if (bit & old->mask) {
            nexts[j] = old->next(i);
            auto const data = old->child_data_view(i);
            memcpy(&hashes[j].data, data.data(), data.size());
            hashes[j].len = data.size();
            old->next(i) = nullptr;
            ++j;
        }
    }
    byte_string_view leaf_data =
        _get_leaf_data(requests.opt_leaf, old->leaf_view());
    return create_node(
        comp,
        mask,
        std::span<ChildData>(hashes, n),
        std::span<Node *>(nexts, n),
        relpath,
        leaf_data);
}

//! split old at old_pi, updates at pi
//! requests can have 1 or more sublists
Node *_mismatch_handler(
    Compute &comp, Node *const old, Requests &requests, unsigned const old_psi,
    unsigned const old_pi, unsigned const pi)
{
    MONAD_DEBUG_ASSERT(!requests.opt_leaf.has_value());
    unsigned char old_nibble = get_nibble(old->path_data(), old_pi);
    uint16_t mask = 1u << old_nibble | requests.mask;
    unsigned n = bitmask_count(mask);
    MONAD_DEBUG_ASSERT(n > 1);
    ChildData hashes[n];
    Node *nexts[n];
    for (unsigned i = 0, j = 0, bit = 1; i < 16; ++i, bit <<= 1) {
        if (bit & requests.mask) {
            if (i == old_nibble) {
                nexts[j] = _upsert(
                    comp, old, std::move(requests)[i], pi + 1, old_pi + 1);
            }
            else {
                nexts[j] =
                    _create_new_trie(comp, std::move(requests)[i], pi + 1);
            }
            hashes[j].len = comp.compute(hashes[j].data, nexts[j]);
            ++j;
        }
        else if (i == old_nibble) {
            // nexts[j] is a path-shortened old node, trim prefix
            NibblesView relpath{old_pi, old->path_ei, old->path_data()};
            nexts[j] = create_node_from_prev(old, relpath, old->leaf_view());
            hashes[j].len = comp.compute(hashes[j].data, nexts[j]);
            ++j;
        }
    }
    // trim suffix
    NibblesView relpath{old_psi, old_pi, old->path_data()};
    return create_node( // no leaf
        comp,
        mask,
        std::span<ChildData>(hashes, n),
        std::span<Node *>(nexts, n),
        relpath);
}

MONAD_MPT_NAMESPACE_END