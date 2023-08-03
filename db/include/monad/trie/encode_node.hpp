#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/core/keccak.h>
#include <monad/core/nibble.h>
#include <monad/rlp/encode.hpp>

#include <monad/trie/compact_encode.hpp>
#include <monad/trie/node.hpp>

#include <cassert>

MONAD_TRIE_NAMESPACE_BEGIN

// return length of noderef
inline uint8_t
to_node_reference(byte_string_view rlp, unsigned char *dest) noexcept
{
    if (MONAD_LIKELY(rlp.size() >= sizeof(merkle_child_info_t::noderef_t))) {
        keccak256(rlp.data(), rlp.size(), dest);
        return 32;
    }
    else {
        memcpy(dest, rlp.data(), rlp.size());
        return rlp.size();
    }
}

/* two-piece RLP:
 *  1. HP(non-redundant key path)
 *  2. value
 *  rlp encode the 2-element list to bytes
 *  keccak the rlp encoded bytes if len >= 32
 * @return: length of the result
 */
inline uint8_t encode_two_piece(
    byte_string_view const first, byte_string_view const second,
    unsigned const second_offset, unsigned char *const dest)
{
    assert(second.size() > second_offset);
    auto second_to_use = byte_string_view{
        second.data() + second_offset, second.size() - second_offset};
    size_t first_len = rlp::string_length(first),
           second_len = rlp::string_length(second_to_use);
    assert(first_len + second_len <= 160);
    unsigned char rlp_string[160];
    auto result = rlp::encode_string(rlp_string, first);
    result = rlp::encode_string(result, second_to_use);
    assert(
        (unsigned long)(result.data() - rlp_string) == first_len + second_len);
    byte_string_view encoded_strings =
        byte_string_view{rlp_string, first_len + second_len};

    size_t rlp_len = rlp::list_length(encoded_strings);
    assert(rlp_len <= 160);
    unsigned char rlp[160];
    rlp::encode_list(rlp, encoded_strings);
    return to_node_reference(byte_string_view(rlp, rlp_len), dest);
}

inline void encode_leaf(
    merkle_node_t *const parent, unsigned char const child_idx,
    byte_string_view const value, bool const is_account)
{
    /* If is_account, value = [offset to storage trie, rlp(account)]
       Else, value = rlp(storage)
    */
    // TODO: first byte of rlp(storage) is always 0x80 + 32, remove the byte on
    // disk
    merkle_child_info_t *child = &parent->children()[child_idx];
    // reallocate if size changed
    if (child->data) {
        if (child->data_len() != value.size()) {
            child->data.resize(value.size());
        }
    }
    else {
        child->data =
            allocators::make_resizeable_unique_for_overwrite<unsigned char[]>(
                value.size());
    }
    child->set_data_len(value.size());
    std::memcpy(child->data.get(), value.data(), value.size());
    unsigned char relpath[sizeof(merkle_child_info_t::path) + 1];
    child->set_noderef_len(encode_two_piece(
        compact_encode(
            relpath,
            child->path,
            parent->path_len + 1,
            child->path_len(),
            true),
        byte_string_view{child->data.get(), child->data_len()},
        is_account ? ROOT_OFFSET_SIZE : 0,
        child->noderef.data()));
}

inline uint8_t encode_branch(merkle_node_t *const branch, unsigned char *dest)
{
#ifndef NDEBUG
    auto str_rlp_len = [](int n) {
        return rlp::string_length(byte_string(32, 1)) * n +
               rlp::string_length({}) * (17 - n);
    }(branch->size());
    assert(str_rlp_len <= 544);
#endif
    unsigned char branch_str_rlp[544];
    std::span<unsigned char> result = branch_str_rlp;
    for (int i = 0, tmp = 1; i < 16; ++i, tmp <<= 1) {
        if (branch->valid_mask & tmp) {
            merkle_child_info_t *child =
                &branch->children()[merkle_child_index(branch, i)];
            if (child->noderef_len() < 32) {
                // meaning the child's noderef is rlp encoded but not keccaked,
                // not need to encode the encoded bytes again here
                memcpy(
                    result.data(), child->noderef.data(), child->noderef_len());
                result = result.subspan(child->noderef_len());
            }
            else {
                result = rlp::encode_string(
                    result,
                    byte_string_view{
                        child->noderef.data(), child->noderef_len()});
            }
        }
        else {
            result = rlp::encode_string(result, byte_string{});
        }
    }
    // encode empty value string
    result = rlp::encode_string(result, byte_string{});
    byte_string_view encoded_strings(
        branch_str_rlp, result.data() - branch_str_rlp);
    size_t branch_rlp_len = rlp::list_length(encoded_strings);
    assert(branch_rlp_len <= 544);
    unsigned char branch_rlp[544];
    rlp::encode_list(branch_rlp, encoded_strings);
    return to_node_reference(
        byte_string_view(branch_rlp, branch_rlp_len), dest);
}

/* Note that when branch_node path > 0, our branch is Extension + Branch
 *  nodes in Ethereum
 * 1. encode branch node
 *   - represent the node as an array of 17 elements (17-th for attached
 leaf)
 *   - rlp encode the array, and keccak the rlp encoded bytes
 * 2. encode extension node: two-piece RLP encoding
 *   - first piece is HP(non-redundant part of the key)
 *   - second is the hash of the branch node representing the prefix group
 */
inline void encode_branch_extension(
    merkle_node_t *const parent, unsigned char const child_idx)
{
    merkle_child_info_t *const child = &parent->children()[child_idx];
    if (!partial_path_len(parent, child_idx)) {
        child->set_noderef_len(
            encode_branch(child->next.get(), child->noderef.data()));
    }
    else {
        // hash both branch and extension
        // TODO: avoid dup copy the branch node ref
        unsigned char branch_ref[32];
        child->set_data_len(encode_branch(child->next.get(), branch_ref));
        child->data =
            allocators::make_resizeable_unique_for_overwrite<unsigned char[]>(
                child->data_len());
        memcpy(child->data.get(), branch_ref, child->data_len());

        unsigned char relpath[sizeof(merkle_child_info_t::path) + 1];
        child->set_noderef_len(encode_two_piece(
            compact_encode(
                relpath,
                child->path,
                parent->path_len + 1,
                child->path_len(),
                false),
            byte_string_view{child->data.get(), child->data_len()},
            0,
            child->noderef.data()));
    }
}

MONAD_TRIE_NAMESPACE_END
