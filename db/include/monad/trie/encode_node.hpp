#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/rlp/encode.hpp>

#include <monad/trie/compact_encode.hpp>
#include <monad/trie/node.hpp>

#include <ethash/keccak.hpp>

#include <cstddef>

#if __GNUC__ == 12 && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Warray-bounds" // is broken on GCC 12
    #pragma GCC diagnostic ignored "-Wstringop-overread" // is broken on GCC 12
#endif

MONAD_TRIE_NAMESPACE_BEGIN

static inline void to_node_reference(byte_string_view rlp, unsigned char *data)
{
    if (rlp.size() >= 32) {
        memcpy(data, ethash::keccak256(rlp.data(), rlp.size()).bytes, 32);
    }
    else {
        memcpy(data, rlp.data(), rlp.size());
    }
}

/* two-piece RLP:
 *  1. HP(non-redundant key path)
 *  2. value
 *  rlp encode the 2-element list to bytes
 *  keccak the rlp encoded bytes if len >= 32
 */
static inline void encode_two_piece(
    byte_string_view const first, byte_string_view const second,
    unsigned char *const data)
{
    size_t first_len = rlp::string_length(first),
           second_len = rlp::string_length(second);
    unsigned char rlp_string[first_len + second_len];
    unsigned char *result = rlp::encode_string(rlp_string, first);
    result = rlp::encode_string(result, second);
    assert((unsigned long)(result - rlp_string) == first_len + second_len);
    byte_string_view encoded_strings =
        byte_string_view{rlp_string, first_len + second_len};

    size_t rlp_len = rlp::list_length(encoded_strings);
    unsigned char rlp[rlp_len];
    rlp::encode_list(rlp, encoded_strings);
    to_node_reference(byte_string_view(rlp, rlp_len), data);
}

static inline void encode_leaf(
    merkle_node_t *const parent, unsigned char const child_idx,
    byte_string_view const value)
{
    merkle_child_info_t *child = &parent->children[child_idx];
    // reallocate if size changed
    child->data = child->data
                      ? child->data_len == value.size()
                            ? child->data
                            : static_cast<unsigned char *>(
                                  realloc(child->data, value.size()))
                      : static_cast<unsigned char *>(malloc(value.size()));
    child->data_len = value.size();
    std::memcpy(child->data, value.data(), value.size());
    unsigned char relpath[33];
    encode_two_piece(
        compact_encode(
            relpath, child->path, parent->path_len + 1, child->path_len, true),
        byte_string_view{child->data, child->data_len},
        child->noderef);
}

static inline void
encode_branch(merkle_node_t *const branch, unsigned char *data)
{
    auto str_rlp_len = [](int n) {
        return rlp::string_length(byte_string(32, 1)) * n +
               rlp::string_length({}) * (17 - n);
    }(branch->nsubnodes);
    unsigned char branch_str_rlp[str_rlp_len];
    unsigned char *result = branch_str_rlp;
    for (int i = 0, tmp = 1; i < 16; ++i, tmp <<= 1) {
        if (branch->valid_mask & tmp) {
            result = rlp::encode_string(
                result,
                byte_string_view{
                    branch->children[merkle_child_index(branch, i)].noderef,
                    32});
        }
        else {
            result = rlp::encode_string(result, byte_string{});
        }
    }
    // encode empty value string
    result = rlp::encode_string(result, byte_string{});
    byte_string_view encoded_strings(branch_str_rlp, result - branch_str_rlp);
    size_t branch_rlp_len = rlp::list_length(encoded_strings);
    unsigned char branch_rlp[branch_rlp_len];
    rlp::encode_list(branch_rlp, encoded_strings);
    to_node_reference(byte_string_view(branch_rlp, branch_rlp_len), data);
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
static inline void encode_branch_extension(
    merkle_node_t *const parent, unsigned char const child_idx)
{
    merkle_child_info_t *child = &parent->children[child_idx];
    if (!partial_path_len(parent, child_idx)) {
        encode_branch(
            child->next, reinterpret_cast<unsigned char *>(&child->noderef));
    }
    else {
        // hash both branch and extension
        child->data_len = 32;
        child->data = static_cast<unsigned char *>(std::malloc(32));
        encode_branch(child->next, child->data);
        unsigned char relpath[33];
        encode_two_piece(
            compact_encode(
                relpath,
                child->path,
                parent->path_len + 1,
                child->path_len,
                false),
            byte_string_view{child->data, child->data_len},
            reinterpret_cast<unsigned char *>(&child->noderef));
    }
}

MONAD_TRIE_NAMESPACE_END

#if __GNUC__ == 12 && !defined(__clang__)
    #pragma GCC diagnostic pop
#endif
