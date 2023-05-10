#include <ethash/keccak.hpp>
#include <monad/core/byte_string.hpp>
#include <monad/merkle/node.h>
#include <monad/rlp/encode.hpp>
#include <monad/trie/nibble.h>

using namespace monad;
using namespace monad::rlp;

/* See Appendix C. HexPrefix from Ethereum Yellow Paper or
    "Specification: Compact encoding of hex sequence with optional
    terminator" in
    https://github.com/ethereum/wiki/wiki/Patricia-Tree/2f1eab18d03c4a6287ea8e659bd9ec88af0ff00a
   - path is non-redundant
   - path_len in number of nibbbles
   TODO: unit tests
*/
static byte_string_view hex_prefix(
    unsigned char *const path, uint8_t const si, uint8_t const ei,
    bool terminating)
{ // HP for path[si, ei) -- index in nibbles
    unsigned ci = si, path_len = ei - si;
    unsigned char *res = (unsigned char *)malloc(path_len / 2 + 1);
    const bool odd = (path_len & 1u) != 0;
    res[0] = terminating ? 0x20 : 0x00;

    if (odd) {
        res[0] |= 0x10;
        res[0] |= get_nibble(path, ci);
        ++ci;
    }
    int res_ci = 2;
    while (ci != ei) {
        set_nibble(res, res_ci++, get_nibble(path, ci++));
    }
    return byte_string_view{res, path_len / 2 + 1};
}

// TODO: only hash if encoded > 32 bytes
void hash_two_piece(
    unsigned char *const path, uint8_t const si, uint8_t const ei,
    bool terminating, unsigned char const *const value,
    unsigned char *const hash)
{
    /* two-piece RLP:
     *  1. HP(non-redundant key path)
     *  2. value
     *  rlp encode the 2-element list to bytes
     *  Lastly keccak the rlp encoded bytes
     */
    byte_string_view hp_path = hex_prefix(path, si, ei, terminating);
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
    memcpy(hash, ethash_keccak256((uint8_t *)rlp, rlp_len).bytes, 32);
    free((void *)hp_path.data());
}

// TODO: rename to leaf_ref()
void hash_leaf(
    merkle_node_t *const parent, unsigned char const child_idx,
    unsigned char const *const value)
{
    merkle_child_info_t *child = &parent->children[child_idx];
    memcpy(child->data, value, 32);
    hash_two_piece(
        child->path,
        parent->path_len + 1,
        child->path_len,
        true,
        child->data,
        (unsigned char *)&child->noderef);
}

// TODO: rename to branch_ref()
void hash_branch(merkle_node_t *const branch, unsigned char *data)
{
    // TODO: need a better way of estimating rlp length
    unsigned char branch_str_rlp
        [string_length(byte_string_view{
             (unsigned char *)&branch->children[0].noderef, 32}) *
             branch->nsubnodes +
         256];
    unsigned char *result = branch_str_rlp;
    for (int i = 0, tmp = 1; i < 16; ++i, tmp <<= 1) {
        if (branch->valid_mask & tmp) {
            result = encode_string(
                result,
                byte_string_view{
                    (unsigned char *)&branch
                        ->children[merkle_child_index(branch, i)]
                        .noderef,
                    32});
        }
        else {
            result = encode_string(result, byte_string{});
        }
    }
    // encode empty string
    result = encode_string(result, byte_string{});
    byte_string_view encoded_strings = byte_string_view(branch_str_rlp, result);
    size_t branch_rlp_len = list_length(encoded_strings);
    unsigned char branch_rlp[branch_rlp_len];
    encode_list(branch_rlp, encoded_strings);
    memcpy(
        data, ethash_keccak256((uint8_t *)branch_rlp, branch_rlp_len).str, 32);
}

void hash_branch_extension(
    merkle_node_t *const parent, unsigned char const child_idx)
{
    /* Note that when branch_node path > 0, our branch is Extension + Branch
       nodes in Ethereum
       1. on producing Eth branch node hash:
       - represent the node as an array of 17 elements (17-th for attached leaf)
       - empty string for position in the array without corresponding elements
       - rlp encode the array, and keccak the rlp encoded bytes
       2. on producing Eth extension node hash:
       - two-piece RLP:
          - first piece is HP(non-redundant part of the key)
          - second is the hash of the branch node representing the prefix group
    */
    merkle_child_info_t *child = &parent->children[child_idx];
    if (child->path_len - parent->path_len == 1) {
        hash_branch(child->next, (unsigned char *)&child->noderef);
    }
    else {
        // hash both branch and extension
        child->data = (unsigned char *)malloc(32);
        hash_branch(child->next, child->data);
        hash_two_piece(
            child->path,
            parent->path_len + 1,
            child->path_len,
            false,
            child->data,
            (unsigned char *)&child->noderef);
    }
}
