#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/globals.hpp>
#include <monad/trie/request.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

struct merkle_node_t;
struct tnode_t;
enum class uring_data_type_t : unsigned char;

struct update_uring_data_t
{
    uring_data_type_t rw_flag;
    char pad[7];
    // read buffer
    unsigned char *buffer;
    int64_t offset;
    Request *updates;
    merkle_node_t *new_parent;
    tnode_t *parent_tnode;
    int16_t buffer_off;
    unsigned char pi;
    uint8_t new_child_ni;
};

static_assert(sizeof(update_uring_data_t) == 56);
static_assert(alignof(update_uring_data_t) == 8);

static inline update_uring_data_t *get_update_uring_data(
    Request *updates, unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode)
{
    update_uring_data_t *user_data =
        reinterpret_cast<update_uring_data_t *>(cpool_ptr29(
            tmppool_, cpool_reserve29(tmppool_, sizeof(update_uring_data_t))));
    cpool_advance29(tmppool_, sizeof(update_uring_data_t));

    // prep uring data
    int64_t node_offset =
        updates->prev_parent->children[updates->prev_child_i].fnext;
    int64_t offset = round_down_4k(node_offset);
    int16_t buffer_off = node_offset - offset;

    update_uring_data_t tmp_data{
        .rw_flag = uring_data_type_t::IS_READ,
        .pad = {},
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