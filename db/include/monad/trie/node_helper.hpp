#pragma once

#include <monad/core/assert.h>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/node.hpp>

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

static inline size_t get_merkle_node_size(uint8_t const nsubnodes)
{
    return sizeof(merkle_node_t) + nsubnodes * sizeof(merkle_child_info_t);
}

static inline merkle_node_t *read_node(int fd, uint64_t node_offset)
{ // blocking read
    int64_t offset = (node_offset >> 9) << 9;
    int16_t buffer_off = node_offset - offset;
    unsigned char *buffer =
        static_cast<unsigned char *>(std::malloc(1UL << 11));
    MONAD_ASSERT(offset == lseek(fd, offset, SEEK_SET));
    MONAD_ASSERT(read(fd, buffer, 1UL << 11) != -1);
    merkle_node_t *node = deserialize_node_from_buffer(buffer + buffer_off, 0);
    free(buffer);
    return node;
}

static inline void free_node(merkle_node_t *const node)
{
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].data) {
            free(node->children[i].data);
        }
    }
    free(node);
}

static inline size_t get_disk_node_size(merkle_node_t const *const node)
{
    size_t total = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        if (node->children[i].data) {
            assert(
                partial_path_len(node, i) > 1 ||
                node->children[i].path_len == 64);
            total += node->children[i].data_len + 1;
        }
        total += (node->children[i].path_len + 1) / 2 - node->path_len / 2;
    }
    return SIZE_OF_SUBNODE_BITMASK + total +
           merkle_child_count_valid(node) *
               (SIZE_OF_NODE_REF + SIZE_OF_FILE_OFFSET + SIZE_OF_PATH_LEN);
}

static inline merkle_node_t *
get_new_merkle_node(uint16_t const mask, unsigned char path_len)
{
    uint8_t const nsubnodes = std::popcount(mask);
    size_t const size = get_merkle_node_size(nsubnodes);
    auto const new_branch = static_cast<merkle_node_t *>(calloc(1, size));
    new_branch->nsubnodes = nsubnodes;
    new_branch->mask = mask;
    new_branch->valid_mask = mask;
    new_branch->path_len = path_len;
    return new_branch;
}

static inline merkle_node_t *
copy_merkle_node_except(merkle_node_t *prev_node, uint8_t except_i)
{ // only copy valid subnodes
    int nsubnodes = merkle_child_count_valid(prev_node);
    auto const copy = static_cast<merkle_node_t *>(
        calloc(1, get_merkle_node_size(nsubnodes)));
    copy->mask = prev_node->valid_mask;
    copy->valid_mask = copy->mask;
    copy->path_len = prev_node->path_len;
    copy->nsubnodes = nsubnodes;
    for (int i = 0, copy_child_i = 0; i < 16; ++i) {
        if (copy->mask & 1u << i) {
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