#pragma once

#include <monad/core/assert.h>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/node.hpp>

#include <memory>

#define MAX_DISK_NODE_SIZE 1536

MONAD_TRIE_NAMESPACE_BEGIN

// helper functions
void free_trie(merkle_node_t *const node);

void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node);

merkle_node_t *deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len);

void assign_prev_child_to_new(
    merkle_node_t *prev_parent, uint8_t prev_child_i, merkle_node_t *new_parent,
    uint8_t new_child_i);

void connect_only_grandchild(merkle_node_t *parent, uint8_t child_idx);

inline size_t get_merkle_node_size(uint8_t const nsubnodes)
{
    return sizeof(merkle_node_t) + nsubnodes * sizeof(merkle_child_info_t);
}

inline merkle_node_t *read_node(int fd, file_offset_t node_offset)
{ // blocking read
    file_offset_t offset = round_down_align<DISK_PAGE_BITS>(node_offset);
    file_offset_t buffer_off = node_offset - offset;
    size_t bytestoread =
        round_up_align<DISK_PAGE_BITS>(MAX_DISK_NODE_SIZE + buffer_off);
    auto buffer = std::make_unique<unsigned char[]>(bytestoread);
    MONAD_ASSERT(
        pread(fd, buffer.get(), bytestoread, offset) == ssize_t(bytestoread));
    merkle_node_t *node =
        deserialize_node_from_buffer(buffer.get() + buffer_off, 0);
    return node;
}

inline void free_node(merkle_node_t *const node)
{
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].data) {
            free(node->children[i].data);
        }
    }
    free(node);
}

inline size_t get_disk_node_size(merkle_node_t const *const node)
{
    constexpr unsigned size_of_node_ref = 32;
    size_t total = 0;
    for (uint16_t i = 0, bit = 1; i < node->nsubnodes; ++i, bit <<= 1) {
        if (node->tomb_arr_mask & bit) {
            continue;
        }
        if (node->children[i].data) {
            assert(
                partial_path_len(node, i) || node->children[i].path_len == 64);
            total += node->children[i].data_len + 1;
        }
        total += (node->children[i].path_len + 1) / 2 - node->path_len / 2;
    }
    return total + sizeof(merkle_node_t::mask_t) +
           merkle_child_count_valid(node) *
               (size_of_node_ref + sizeof(merkle_child_info_t::fnext_t) +
                sizeof(merkle_child_info_t::path_len_t));
}

inline merkle_node_t *
get_new_merkle_node(uint16_t const mask, unsigned char path_len)
{
    uint8_t const nsubnodes = std::popcount(mask);
    size_t const size = get_merkle_node_size(nsubnodes);
    auto *new_branch = static_cast<merkle_node_t *>(calloc(1, size));
    MONAD_ASSERT(new_branch != nullptr);
    new_branch->nsubnodes = nsubnodes;
    new_branch->mask = mask;
    new_branch->valid_mask = mask;
    new_branch->path_len = path_len;
    return new_branch;
}

inline merkle_node_t *
copy_merkle_node_except(merkle_node_t *prev_node, uint8_t except_i)
{ // only copy valid subnodes
    int nsubnodes = merkle_child_count_valid(prev_node);
    auto *copy = static_cast<merkle_node_t *>(
        calloc(1, get_merkle_node_size(nsubnodes)));
    MONAD_ASSERT(copy != nullptr);
    copy->mask = prev_node->valid_mask;
    copy->valid_mask = copy->mask;
    copy->path_len = prev_node->path_len;
    copy->nsubnodes = nsubnodes;
    for (uint16_t i = 0, copy_child_i = 0, bit = 1; i < 16; ++i, bit <<= 1) {
        if (copy->mask & bit) {
            if (i != except_i) {
                assign_prev_child_to_new(
                    prev_node,
                    merkle_child_index(prev_node, i),
                    copy,
                    copy_child_i);
            }
            ++copy_child_i;
        }
    }
    return copy;
}

MONAD_TRIE_NAMESPACE_END