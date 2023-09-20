#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/rlp/encode.hpp>

#include <monad/mpt/config.hpp>
#include <monad/mpt/merkle/compact_encode.hpp>
#include <monad/mpt/merkle/node_reference.hpp>
#include <monad/mpt/node.hpp>

#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute
{
    virtual ~Compute(){};
    //! compute length of hash from a span of child data and child node pointers
    virtual unsigned compute_len(
        std::span<ChildData> const hashes, std::span<node_ptr> const nexts) = 0;
    //! compute hash_data inside node if hash_len > 0, which is the hash of all
    //! node's branches, return hash data length
    virtual unsigned
    compute_branch(unsigned char *const buffer, Node *const node) = 0;
    //! compute data of a trie rooted at node, put data to first argument and
    //! return data length. 3rd parameter is the branch nibble of node, it's
    //! present only when node is the single child of its parent, which is a
    //! leaf node
    virtual unsigned compute(unsigned char *const buffer, Node *const node) = 0;
};

struct EmptyCompute final : Compute
{
    virtual unsigned
    compute_len(std::span<ChildData> const, std::span<node_ptr> const) override
    {
        return 0;
    }

    virtual unsigned compute_branch(unsigned char *const, Node *) override
    {
        return 0;
    }

    virtual unsigned compute(unsigned char *const, Node *) override
    {
        return 0;
    }
};

struct MerkleCompute final : Compute
{
    virtual unsigned compute_len(
        std::span<ChildData> const hashes,
        std::span<node_ptr> const nexts) override
    {
        if (hashes.size() == 1) {
            // special case, the node to be created has only one branch
            return _compute_hash_with_extra_nibble_to_state(
                nexts[0].get(), hashes[0]);
        }

        unsigned char branch_str_rlp[544];
        std::span<unsigned char> result = branch_str_rlp;
        // more than 1 child branch
        unsigned i = 0;
        for (unsigned j = 0; j < hashes.size(); ++i) {
            if (hashes[j].branch == i) {
                result =
                    (hashes[j].len < 32)
                        ? [&] {
                              memcpy(
                                  result.data(), hashes[j].data, hashes[j].len);
                              return result.subspan(hashes[j].len);
                          }()
                        : rlp::encode_string(result, {hashes[j].data, hashes[j].len});
                ++j;
            }
            else {
                result[0] = RLP_EMPTY_STRING;
                result = result.subspan(1);
                if (hashes[j].branch == INVALID_BRANCH) {
                    ++j;
                }
            }
        }
        // encode empty value string
        for (; i < 17; ++i) {
            result[0] = RLP_EMPTY_STRING;
            result = result.subspan(1);
        }
        MONAD_ASSERT(result.data() - branch_str_rlp <= 544);
        byte_string_view encoded_strings{
            branch_str_rlp,
            static_cast<size_t>(result.data() - branch_str_rlp)};
        size_t branch_rlp_len = rlp::list_length(encoded_strings);
        MONAD_DEBUG_ASSERT(branch_rlp_len <= 544);

        unsigned char branch_rlp[544];
        rlp::encode_list(branch_rlp, encoded_strings);
        // compute hash to internal state and return hash length
        return state.len = to_node_reference(
                   {branch_rlp, branch_rlp_len}, state.buffer);
    }

    virtual unsigned
    compute_branch(unsigned char *const buffer, Node *const node) override
    {
        MONAD_DEBUG_ASSERT(node->n());
        if (state.len) {
            // a simple memcpy if already computed to internal state
            std::memcpy(buffer, state.buffer, state.len);
            unsigned const len = state.len;
            state.len = 0;
            return len;
        }
        MONAD_DEBUG_ASSERT(node->n() > 1);
        unsigned char branch_str_rlp[544];
        std::span<unsigned char> result = branch_str_rlp;
        for (unsigned i = 0, bit = 1; i < 16; ++i, bit <<= 1) {
            if (node->mask & bit) {
                result =
                    (node->child_data_len(i) < 32)
                        ? [&] {
                              memcpy(
                                  result.data(),
                                  node->child_data(i),
                                  node->child_data_len(i));
                              return result.subspan(node->child_data_len(i));
                          }()
                        : rlp::encode_string(result, node->child_data_view(i));
            }
            else {
                result[0] = RLP_EMPTY_STRING;
                result = result.subspan(1);
            }
        }
        // encode empty value string
        result[0] = RLP_EMPTY_STRING;
        result = result.subspan(1);
        MONAD_ASSERT(result.data() - branch_str_rlp <= 544);
        byte_string_view encoded_strings{
            branch_str_rlp,
            static_cast<size_t>(result.data() - branch_str_rlp)};
        size_t branch_rlp_len = rlp::list_length(encoded_strings);
        MONAD_DEBUG_ASSERT(branch_rlp_len <= 544);

        unsigned char branch_rlp[544];
        rlp::encode_list(branch_rlp, encoded_strings);
        return to_node_reference({branch_rlp, branch_rlp_len}, buffer);
    }

    virtual unsigned
    compute(unsigned char *const buffer, Node *const node) override
    {
        if (node->is_leaf) {
            return _encode_two_pieces(
                buffer,
                node->path_nibble_view(),
                _compute_leaf_data(node),
                true);
        }
        MONAD_DEBUG_ASSERT(node->n() > 1);
        if (node->has_relpath()) {
            unsigned char hash[32];
            unsigned len = compute_branch(hash, node);
            return _encode_two_pieces(
                buffer, node->path_nibble_view(), {hash, len});
        }
        return compute_branch(buffer, node);
    }

private:
    struct InternalHashState
    {
        unsigned char buffer[32]; // max 32 bytes
        unsigned len{0};
    } state{};

    // TEMPORARY for POC
    // compute leaf data as - concat2(input_leaf, hash);
    byte_string _compute_leaf_data(Node const *const node)
    {
        return byte_string{node->leaf_view()} + byte_string{node->hash_view()};
    }

    unsigned _encode_two_pieces(
        unsigned char *const dest, NibblesView relpath,
        byte_string_view const second, bool const is_leaf = false)
    {
        unsigned char path_arr[56];
        auto first = compact_encode(path_arr, relpath, is_leaf);
        // leaf and hashed node ref requires rlp encoding,
        // rlp encoded but unhashed branch node ref doesn't
        bool const need_encode_second = is_leaf || second.size() >= 32;
        size_t first_len = rlp::string_length(first),
               second_len = need_encode_second ? rlp::string_length(second)
                                               : second.size();
        MONAD_DEBUG_ASSERT(first_len + second_len <= 160);
        unsigned char rlp_string[160];
        auto result = rlp::encode_string(rlp_string, first);
        result = need_encode_second ? rlp::encode_string(result, second) : [&] {
            memcpy(result.data(), second.data(), second.size());
            return result.subspan(second.size());
        }();
        MONAD_DEBUG_ASSERT(
            (unsigned long)(result.data() - rlp_string) ==
            first_len + second_len);

        byte_string_view encoded_strings{rlp_string, first_len + second_len};
        size_t rlp_len = rlp::list_length(encoded_strings);
        MONAD_DEBUG_ASSERT(rlp_len <= 160);
        unsigned char rlp[160];
        rlp::encode_list(rlp, encoded_strings);
        return to_node_reference({rlp, rlp_len}, dest);
    }

    unsigned
    _compute_hash_with_extra_nibble_to_state(Node *const node, ChildData &hash)
    {
        state.len = 0;

        return state.len = _encode_two_pieces(
                   state.buffer,
                   concat2(hash.branch, node->path_nibble_view()),
                   (node->is_leaf
                        ? _compute_leaf_data(node)
                        : (node->has_relpath()
                               ? ([&] -> byte_string {
                                     unsigned char branch_hash[32];
                                     return {
                                         branch_hash,
                                         compute_branch(branch_hash, node)};
                                 }())
                               : byte_string_view{hash.data, hash.len})),
                   node->is_leaf);
    }
};

MONAD_MPT_NAMESPACE_END