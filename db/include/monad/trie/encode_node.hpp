#pragma once

#include <cstddef>
#include <ethash/keccak.hpp>
#include <monad/trie/compact_encode.hpp>
#include <monad/trie/node.hpp>

#include <monad/core/byte_string.hpp>
#include <monad/core/nibble.h>
#include <monad/rlp/encode.hpp>

using namespace monad;
using namespace monad::rlp;
using namespace monad::trie;

MONAD_TRIE_NAMESPACE_BEGIN
/* two-piece RLP:
 *  1. HP(non-redundant key path)
 *  2. value
 *  rlp encode the 2-element list to bytes
 *  Lastly keccak the rlp encoded bytes
 */

static inline void hash_two_piece(
    unsigned char *const path, uint8_t const si, uint8_t const ei,
    bool terminating, unsigned char const *const value,
    unsigned char *const hash)
{
    // TODO: only hash if encoded > 32 bytes
    byte_string_view hp_path = compact_encode(path, si, ei, terminating);
    byte_string_view value_view{value, 32};
    size_t hp_rlp_len = string_length(hp_path),
           value_rlp_len = string_length(value_view);
    unsigned char rlp_string[hp_rlp_len + value_rlp_len];
    unsigned char *result = encode_string(rlp_string, hp_path);
    result = encode_string(result, value_view);
    assert((unsigned long)(result - rlp_string) == hp_rlp_len + value_rlp_len);
    byte_string_view encoded_strings =
        byte_string_view{rlp_string, hp_rlp_len + value_rlp_len};

    size_t rlp_len = list_length(encoded_strings);
    unsigned char rlp[rlp_len];
    encode_list(rlp, encoded_strings);
    std::memcpy(hash, ethash_keccak256((uint8_t *)rlp, rlp_len).bytes, 32);
    free(static_cast<void *>(const_cast<unsigned char *>(hp_path.data())));
}

static inline void encode_leaf(
    merkle_node_t *const parent, unsigned char const child_idx,
    unsigned char const *const value)
{
    merkle_child_info_t *child = &parent->children[child_idx];
    std::memcpy(child->data, value, 32);
    hash_two_piece(
        child->path,
        parent->path_len + 1,
        child->path_len,
        true,
        child->data,
        reinterpret_cast<unsigned char *>(&child->noderef));
}

static inline void
encode_branch(merkle_node_t *const branch, unsigned char *data)
{
    // unsigned char branch_str_rlp
    //     [string_length(to_byte_string_view((unsigned char[]){32})) *
    //          branch->nsubnodes +
    //      string_length({}) * (33 - branch->nsubnodes)];

    unsigned char branch_str_rlp
        [string_length(byte_string_view{
             reinterpret_cast<unsigned char *>(&branch->children[0].noderef),
             32}) *
             branch->nsubnodes +
         256];
    unsigned char *result = branch_str_rlp;
    for (int i = 0, tmp = 1; i < 16; ++i, tmp <<= 1) {
        if (branch->valid_mask & tmp) {
            result = encode_string(
                result,
                byte_string_view{
                    reinterpret_cast<unsigned char *>(
                        &branch->children[merkle_child_index(branch, i)]
                             .noderef),
                    32});
        }
        else {
            result = encode_string(result, byte_string{});
        }
    }
    // encode empty string
    result = encode_string(result, byte_string{});
    byte_string_view encoded_strings(branch_str_rlp, result - branch_str_rlp);
    size_t branch_rlp_len = list_length(encoded_strings);
    unsigned char branch_rlp[branch_rlp_len];
    encode_list(branch_rlp, encoded_strings);
    std::memcpy(
        data,
        ethash_keccak256(static_cast<uint8_t *>(branch_rlp), branch_rlp_len)
            .str,
        32);
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
    if (child->path_len - parent->path_len == 1) {
        encode_branch(
            child->next, reinterpret_cast<unsigned char *>(&child->noderef));
    }
    else {
        // hash both branch and extension
        child->data = static_cast<unsigned char *>(std::malloc(32));
        encode_branch(child->next, child->data);
        hash_two_piece(
            child->path,
            parent->path_len + 1,
            child->path_len,
            false,
            child->data,
            reinterpret_cast<unsigned char *>(&child->noderef));
    }
}

MONAD_TRIE_NAMESPACE_END