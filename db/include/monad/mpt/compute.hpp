#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/rlp/encode.hpp>

#include <monad/mpt/cache_option.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/merkle/compact_encode.hpp>
#include <monad/mpt/merkle/node_reference.hpp>
#include <monad/mpt/node.hpp>

#include <algorithm>
#include <cstdint>
#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute
{
    virtual ~Compute(){};
    //! compute length of hash from a span of child data, which include the node
    //! pointer, file offset and calculated hash
    virtual unsigned
    compute_len(std::span<ChildData> children, uint16_t mask) = 0;
    //! compute hash_data inside node if hash_len > 0, which is the hash of all
    //! node's branches, return hash data length
    virtual unsigned compute_branch(unsigned char *buffer, Node *node) = 0;
    //! compute data of a trie rooted at node, put data to first argument and
    //! return data length. 3rd parameter is the branch nibble of node, it's
    //! present only when node is the single child of its parent, which is a
    //! leaf node
    virtual unsigned compute(unsigned char *buffer, Node *node) = 0;
};

struct TrieStateMachine
{
    virtual ~TrieStateMachine(){};
    virtual std::unique_ptr<TrieStateMachine> clone() const = 0;
    //! reset state to default
    virtual void reset(std::optional<uint8_t> sec = std::nullopt) = 0;
    //! forward transition down the trie, with a possible input value
    virtual void forward(byte_string_view = {}) = 0;
    //! transform back up the trie
    virtual void backward() = 0;
    //! get the current compute implementation
    virtual Compute &get_compute() = 0;
    virtual Compute &get_compute(uint8_t sec) = 0;
    //! get current state in uint8_t. It is up to the user to design what
    //! each value means in the state enum
    virtual uint8_t get_state() const = 0;
    //! get the current cache option in CacheOption enum type.
    virtual CacheOption get_cache_option() const = 0;
};

namespace detail
{
    constexpr auto max_branch_rlp_size =
        rlp::list_length(rlp::list_length(32) * 16 + rlp::list_length(0));
    constexpr auto max_leaf_data_size = rlp::list_length( // account rlp
        rlp::list_length(32) // balance
        + rlp::list_length(32) // code hash
        + rlp::list_length(32) // storage hash
        + rlp::list_length(8) // nonce
    );
    static_assert(max_branch_rlp_size == 532);
    static_assert(max_leaf_data_size == 110);

    template <typename T>
    concept compute_leaf_data = requires {
        {
            T::compute(std::declval<Node const &>())
        } -> std::same_as<byte_string>;
    };

    template <compute_leaf_data TComputeLeafData>
    struct MerkleComputeBase : Compute
    {
        // compute the actual data to the internal state
        // Only called when computing data to stored inline in a leaf node
        virtual unsigned compute_len(
            std::span<ChildData> const children, uint16_t const mask) override
        {
            MONAD_DEBUG_ASSERT(std::popcount(mask) >= 1);
            // special case, the node to be created has only one branch
            if (std::has_single_bit(mask)) {
                auto const it = std::ranges::find_if(
                    children, [](ChildData const &item) constexpr {
                        return item.is_valid();
                    });
                MONAD_DEBUG_ASSERT(it != children.end());
                MONAD_DEBUG_ASSERT(it->branch < 16);
                MONAD_DEBUG_ASSERT(it->ptr);
                compute_hash_with_extra_nibble_to_state_(*it);
                return keccak_internal_state_data_inplace();
            }

            unsigned char branch_str_rlp[max_branch_rlp_size];
            std::span<unsigned char> result = branch_str_rlp;
            unsigned i = 0;
            for (auto &child : children) {
                if (child.is_valid()) {
                    MONAD_DEBUG_ASSERT(child.branch < 16);
                    while (child.branch != i) {
                        result[0] = RLP_EMPTY_STRING;
                        result = result.subspan(1);
                        ++i;
                    }
                    MONAD_DEBUG_ASSERT(i == child.branch);
                    result =
                        (child.len < 32)
                            ? [&] {
                                  memcpy(result.data(), child.data, child.len);
                                  return result.subspan(child.len);
                              }()
                            : rlp::encode_string(result, {child.data, child.len});
                    ++i;
                }
            }
            // encode empty value string
            for (; i < 17; ++i) {
                result[0] = RLP_EMPTY_STRING;
                result = result.subspan(1);
            }

            auto const concat_size =
                static_cast<size_t>(result.data() - branch_str_rlp);
            MONAD_DEBUG_ASSERT(concat_size <= max_branch_rlp_size);
            auto const rlp_size = rlp::list_length(concat_size);
            MONAD_DEBUG_ASSERT(rlp_size <= max_branch_rlp_size);

            unsigned char branch_rlp[max_branch_rlp_size];
            rlp::encode_list(
                branch_rlp, byte_string_view{branch_str_rlp, concat_size});
            // Compute hash to internal state and return hash length
            state.len = to_node_reference({branch_rlp, rlp_size}, state.buffer);
            return keccak_internal_state_data_inplace();
        }

        virtual unsigned
        compute_branch(unsigned char *const buffer, Node *const node) override
        {
            MONAD_DEBUG_ASSERT(node->number_of_children());
            if (state.len) {
                // a simple memcpy if already computed to internal state
                std::memcpy(buffer, state.buffer, state.len);
                unsigned const len = state.len;
                state.len = 0;
                return len;
            }
            MONAD_DEBUG_ASSERT(node->number_of_children() > 1);
            unsigned char branch_str_rlp[max_branch_rlp_size];
            std::span<unsigned char> result = branch_str_rlp;
            for (unsigned i = 0, bit = 1; i < 16; ++i, bit <<= 1) {
                if (node->mask & bit) {
                    auto const child_index = node->to_child_index(i);
                    MONAD_DEBUG_ASSERT(node->child_data_len(child_index) <= 32);
                    result =
                        (node->child_data_len(child_index) < 32)
                            ? [&] {
                                  memcpy(
                                      result.data(),
                                      node->child_data(child_index),
                                      node->child_data_len(child_index));
                                  return result.subspan(
                                      node->child_data_len(child_index));
                              }()
                            : rlp::encode_string(result, node->child_data_view(child_index));
                }
                else {
                    result[0] = RLP_EMPTY_STRING;
                    result = result.subspan(1);
                }
            }
            // encode empty value string
            result[0] = RLP_EMPTY_STRING;
            result = result.subspan(1);
            auto const concat_size =
                static_cast<size_t>(result.data() - branch_str_rlp);
            MONAD_ASSERT(concat_size <= max_branch_rlp_size);
            auto const branch_rlp_len = rlp::list_length(concat_size);
            MONAD_DEBUG_ASSERT(branch_rlp_len <= max_branch_rlp_size);

            unsigned char branch_rlp[max_branch_rlp_size];
            rlp::encode_list(
                branch_rlp, byte_string_view{branch_str_rlp, concat_size});
            return to_node_reference({branch_rlp, branch_rlp_len}, buffer);
        }

        virtual unsigned
        compute(unsigned char *const buffer, Node *const node) override
        {
            if (node->has_value()) {
                return encode_two_pieces_(
                    buffer,
                    node->path_nibble_view(),
                    TComputeLeafData::compute(*node),
                    true);
            }
            MONAD_DEBUG_ASSERT(node->number_of_children() > 1);
            if (node->has_path()) {
                unsigned char reference[KECCAK256_SIZE];
                unsigned len = compute_branch(reference, node);
                return encode_two_pieces_(
                    buffer, node->path_nibble_view(), {reference, len});
            }
            return compute_branch(buffer, node);
        }

    private:
        struct InternalHashState
        {
            unsigned char buffer[KECCAK256_SIZE];
            unsigned len{0};
        } state{};

        unsigned encode_two_pieces_(
            unsigned char *const dest, NibblesView const path,
            byte_string_view const second, bool const has_value = false)
        {
            constexpr size_t max_compact_encode_size = KECCAK256_SIZE + 1;
            constexpr size_t max_rlp_size = rlp::list_length(
                rlp::list_length(max_compact_encode_size) +
                rlp::list_length(max_leaf_data_size));
            static_assert(max_compact_encode_size == 33);
            static_assert(max_rlp_size == 148);

            MONAD_DEBUG_ASSERT(path.data_size() <= KECCAK256_SIZE);
            MONAD_DEBUG_ASSERT(second.size() <= max_leaf_data_size);

            unsigned char path_arr[max_compact_encode_size];
            auto const first = compact_encode(path_arr, path, has_value);
            MONAD_ASSERT(first.size() <= max_compact_encode_size);
            // leaf and hashed node ref requires rlp encoding,
            // rlp encoded but unhashed branch node ref doesn't
            bool const need_encode_second = has_value || second.size() >= 32;
            auto const concat_size =
                rlp::string_length(first) + (need_encode_second
                                                 ? rlp::string_length(second)
                                                 : second.size());
            MONAD_DEBUG_ASSERT(concat_size <= max_rlp_size);
            unsigned char rlp_string[max_rlp_size];
            auto result = rlp::encode_string(rlp_string, first);
            result =
                need_encode_second ? rlp::encode_string(result, second) : [&] {
                    memcpy(result.data(), second.data(), second.size());
                    return result.subspan(second.size());
                }();
            MONAD_DEBUG_ASSERT(
                (unsigned long)(result.data() - rlp_string) == concat_size);

            auto const rlp_len = rlp::list_length(concat_size);
            MONAD_DEBUG_ASSERT(rlp_len <= max_rlp_size);
            unsigned char rlp[max_rlp_size];
            rlp::encode_list(rlp, byte_string_view{rlp_string, concat_size});
            return to_node_reference({rlp, rlp_len}, dest);
        }

        unsigned
        compute_hash_with_extra_nibble_to_state_(ChildData &single_child)
        {
            Node *const node = single_child.ptr;
            MONAD_DEBUG_ASSERT(node);

            return state.len = encode_two_pieces_(
                state.buffer,
                concat(single_child.branch, node->path_nibble_view()),
                (node->has_value()
                     ? TComputeLeafData::compute(*node)
                     : (node->has_path()
                            ? ([&] -> byte_string {
                                  unsigned char branch_hash[KECCAK256_SIZE];
                                  return {
                                      branch_hash,
                                      compute_branch(branch_hash, node)};
                              }())
                            : byte_string_view{single_child.data, single_child.len})),
                node->has_value());
        }

        unsigned keccak_internal_state_data_inplace()
        {
            if (state.len < KECCAK256_SIZE) {
                keccak256(state.buffer, state.len, state.buffer);
                state.len = KECCAK256_SIZE;
            }
            MONAD_DEBUG_ASSERT(state.len == KECCAK256_SIZE);
            return state.len;
        }
    };
}

MONAD_MPT_NAMESPACE_END
