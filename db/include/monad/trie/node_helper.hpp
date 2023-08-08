#pragma once

#include <monad/core/assert.h>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/node.hpp>
#include <monad/trie/util.hpp>

#include <filesystem>
#include <memory>
MONAD_TRIE_NAMESPACE_BEGIN

void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node,
    unsigned shouldbe_bytes_written);

merkle_node_ptr deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len);

void assign_prev_child_to_new(
    merkle_node_t *prev_parent, uint8_t prev_child_i, merkle_node_t *new_parent,
    uint8_t new_child_i, bool const is_account);

void connect_only_grandchild(
    merkle_node_t *parent, uint8_t child_idx, bool const is_account);

inline merkle_node_ptr read_node(
    int fd, file_offset_t node_offset, unsigned char const node_path_len = 0)
{ // blocking read
    file_offset_t offset = round_down_align<DISK_PAGE_BITS>(node_offset);
    file_offset_t buffer_off = node_offset - offset;
    size_t bytestoread =
        round_up_align<DISK_PAGE_BITS>(MAX_DISK_NODE_SIZE + buffer_off);
    alignas(DISK_PAGE_SIZE) unsigned char buffer[bytestoread];
    // read size is not always down aligned MAX_DISK_NODE_SIZE because file
    // size can be smaller than that
    MONAD_ASSERT(pread(fd, buffer, bytestoread, offset) > 0);
    return deserialize_node_from_buffer(buffer + buffer_off, node_path_len);
}

inline unsigned get_disk_node_size(merkle_node_t const *const node)
{
    unsigned total = 0, children_valid = 0;
    for (uint16_t i = 0, bit = 1; i < node->size(); ++i, bit <<= 1) {
        if (node->tomb_arr_mask & bit) {
            continue;
        }
        children_valid++;
        if (node->children()[i].data) {
            assert(
                partial_path_len(node, i) ||
                node->children()[i].path_len() == 64);
            total += node->children()[i].data_len();
        }
        total += node->children()[i].noderef_len;
        total += (node->children()[i].path_len() + 1) / 2 - node->path_len / 2;
    }
    total +=
        sizeof(merkle_node_t::mask_t) +
        children_valid * (sizeof(merkle_child_info_t::bitpacked_storage_t) +
                          sizeof(merkle_child_info_t::data_len_t));
    total = (total + 1) & ~1;
    return total;
}

inline merkle_node_ptr
get_new_merkle_node(uint16_t const mask = 0, unsigned char path_len = 0)
{
    uint8_t const nsubnodes = std::popcount(mask);
    auto new_branch = merkle_node_t::make_with_children(nsubnodes);
    new_branch->mask = mask;
    new_branch->valid_mask = mask;
    new_branch->path_len = path_len;
    return new_branch;
}

inline merkle_node_ptr copy_merkle_node_except(
    merkle_node_t *prev_node, uint8_t except_i, bool const is_account)
{ // only copy valid subnodes
    const uint8_t nsubnodes = merkle_child_count_valid(prev_node);
    auto copy = merkle_node_t::make_with_children(nsubnodes);
    copy->mask = prev_node->valid_mask;
    copy->valid_mask = copy->mask;
    copy->path_len = prev_node->path_len;
    for (uint16_t i = 0, copy_child_i = 0, bit = 1; i < 16; ++i, bit <<= 1) {
        if (copy->mask & bit) {
            if (i != except_i) {
                assign_prev_child_to_new(
                    prev_node,
                    merkle_child_index(prev_node, i),
                    copy.get(),
                    copy_child_i,
                    is_account);
            }
            ++copy_child_i;
        }
    }
    return copy;
}

MONAD_TRIE_NAMESPACE_END
