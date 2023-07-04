#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

#include <monad/trie/node.hpp>
#include <monad/trie/request.hpp>

#include <monad/trie/allocators.hpp>

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
    Request::unique_ptr_type updates;
    merkle_node_t *new_parent;
    tnode_t *parent_tnode;
    int16_t buffer_off;
    unsigned char pi;
    uint8_t new_child_ni;

    using allocator_type = boost_unordered_pool_allocator<update_uring_data_t>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        update_uring_data_t, unique_ptr_allocator_deleter<
                                 allocator_type, &update_uring_data_t::pool>>;
    static unique_ptr_type make(update_uring_data_t v)
    {
        return allocate_unique<allocator_type, &update_uring_data_t::pool>(
            std::move(v));
    }
};

static_assert(sizeof(update_uring_data_t) == 64);
static_assert(alignof(update_uring_data_t) == 8);

inline update_uring_data_t::unique_ptr_type get_update_uring_data(
    Request::unique_ptr_type updates, unsigned char pi,
    merkle_node_t *const new_parent, uint8_t const new_child_ni,
    tnode_t *parent_tnode, MerkleTrie *trie)
{
    // prep uring data
    int64_t node_offset =
        updates->prev_parent->children[updates->prev_child_i].fnext;
    int64_t offset = round_down_align(static_cast<uint64_t>(node_offset));
    int16_t buffer_off = node_offset - offset;

    return update_uring_data_t::make(update_uring_data_t{
        .rw_flag = uring_data_type_t::IS_READ,
        .pad = {},
        .trie = trie,
        .buffer = 0,
        .offset = offset,
        .updates = std::move(updates),
        .new_parent = new_parent,
        .parent_tnode = parent_tnode,
        .buffer_off = buffer_off,
        .pi = pi,
        .new_child_ni = new_child_ni,
    });
}

MONAD_TRIE_NAMESPACE_END