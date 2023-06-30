#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

#include <monad/trie/node.hpp>
#include <monad/trie/request.hpp>

#include <boost/pool/object_pool.hpp>

#include <cstddef>

MONAD_TRIE_NAMESPACE_BEGIN

struct tnode_t;
class MerkleTrie;
enum class uring_data_type_t : unsigned char;

struct update_uring_data_t
{
    uring_data_type_t rw_flag;
    char pad[7];
    MerkleTrie *trie;
    // read buffer
    unsigned char *buffer;
    int64_t offset;
    Request *updates;
    merkle_node_t *new_parent;
    tnode_t *parent_tnode;
    int16_t buffer_off;
    unsigned char pi;
    uint8_t new_child_ni;
    static inline boost::object_pool<update_uring_data_t> pool{};
};

static_assert(sizeof(update_uring_data_t) == 64);
static_assert(alignof(update_uring_data_t) == 8);

inline update_uring_data_t *get_update_uring_data(
    Request *const updates, unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode, MerkleTrie *trie)
{
    update_uring_data_t *user_data = update_uring_data_t::pool.malloc();

    // prep uring data
    int64_t node_offset =
        updates->prev_parent->children[updates->prev_child_i].fnext;
    int64_t offset = round_down_align(static_cast<uint64_t>(node_offset));
    int16_t buffer_off = node_offset - offset;

    update_uring_data_t tmp_data{
        .rw_flag = uring_data_type_t::IS_READ,
        .pad = {},
        .trie = trie,
        .buffer = 0,
        .offset = offset,
        .updates = updates,
        .new_parent = new_parent,
        .parent_tnode = parent_tnode,
        .buffer_off = buffer_off,
        .pi = pi,
        .new_child_ni = new_child_ni,
    };

    *user_data = tmp_data;
    return user_data;
}

MONAD_TRIE_NAMESPACE_END