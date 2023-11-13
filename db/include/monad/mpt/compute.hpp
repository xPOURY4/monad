#pragma once

#include <monad/core/assert.h>
#include <monad/core/byte_string.hpp>
#include <monad/rlp/encode.hpp>

#include <monad/mpt/cache_option.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/merkle/compact_encode.hpp>
#include <monad/mpt/merkle/node_reference.hpp>
#include <monad/mpt/node.hpp>

#include <span>

MONAD_MPT_NAMESPACE_BEGIN

struct Compute
{
    virtual ~Compute(){};
    //! compute length of hash from a span of child data, which include the node
    //! pointer, file offset and calculated hash
    virtual unsigned compute_len(std::span<ChildData> children) = 0;
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
    virtual Compute &get_compute() const = 0;
    //! get current state in uint8_t. It is up to the user to design what each
    //! value means in the state enum
    virtual uint8_t get_state() const = 0;
    //! get the current cache option in CacheOption enum type.
    virtual CacheOption cache_option() const = 0;
};

struct EmptyCompute final : Compute
{
    virtual unsigned compute_len(std::span<ChildData> const) override
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

namespace detail
{

    template <typename T>
    concept compute_leaf_data = requires {
        {
            T::compute(std::declval<const Node *>())
        } -> std::same_as<byte_string>;
    };
    template <compute_leaf_data TComputeLeafData>
    struct MerkleComputeBase : Compute
    {
        // compute the actual data to the internal state
        virtual unsigned
        compute_len(std::span<ChildData> const children) override
        {
            if (children.size() == 1) {
                // TODO: not size() == 1 but #valid child = 1
                // special case, the node to be created has only one branch
                return compute_hash_with_extra_nibble_to_state_(children[0]);
            }

            unsigned char branch_str_rlp[544];
            std::span<unsigned char> result = branch_str_rlp;
            // more than 1 child branch
            unsigned i = 0;
            for (unsigned j = 0; j < children.size(); ++i) {
                if (children[j].branch == i) {
                    result =
                    (children[j].len < 32)
                        ? [&] {
                              memcpy(
                                  result.data(), children[j].data, children[j].len);
                              return result.subspan(children[j].len);
                          }()
                        : rlp::encode_string(result, {children[j].data, children[j].len});
                    ++j;
                }
                else {
                    result[0] = RLP_EMPTY_STRING;
                    result = result.subspan(1);
                    if (children[j].branch == INVALID_BRANCH) {
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
            MONAD_DEBUG_ASSERT(node->number_of_children());
            if (state.len) {
                // a simple memcpy if already computed to internal state
                std::memcpy(buffer, state.buffer, state.len);
                unsigned const len = state.len;
                state.len = 0;
                return len;
            }
            MONAD_DEBUG_ASSERT(node->number_of_children() > 1);
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
                                  return result.subspan(
                                      node->child_data_len(i));
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
            if (node->is_leaf()) {
                return encode_two_pieces_(
                    buffer,
                    node->path_nibble_view(),
                    TComputeLeafData::compute(node),
                    true);
            }
            MONAD_DEBUG_ASSERT(node->number_of_children() > 1);
            if (node->has_relpath()) {
                unsigned char hash[32];
                unsigned len = compute_branch(hash, node);
                return encode_two_pieces_(
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

        unsigned encode_two_pieces_(
            unsigned char *const dest, NibblesView const relpath,
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
            result =
                need_encode_second ? rlp::encode_string(result, second) : [&] {
                    memcpy(result.data(), second.data(), second.size());
                    return result.subspan(second.size());
                }();
            MONAD_DEBUG_ASSERT(
                (unsigned long)(result.data() - rlp_string) ==
                first_len + second_len);

            byte_string_view encoded_strings{
                rlp_string, first_len + second_len};
            size_t rlp_len = rlp::list_length(encoded_strings);
            MONAD_DEBUG_ASSERT(rlp_len <= 160);
            unsigned char rlp[160];
            rlp::encode_list(rlp, encoded_strings);
            return to_node_reference({rlp, rlp_len}, dest);
        }

        unsigned
        compute_hash_with_extra_nibble_to_state_(ChildData &single_child)
        {
            state.len = 0;
            Node *const node = single_child.ptr;

            return state.len = encode_two_pieces_(
                   state.buffer,
                   concat2(single_child.branch, node->path_nibble_view()),
                   (node->is_leaf()
                        ? TComputeLeafData::compute(node)
                        : (node->has_relpath()
                               ? ([&] -> byte_string {
                                     unsigned char branch_hash[32];
                                     return {
                                         branch_hash,
                                         compute_branch(branch_hash, node)};
                                 }())
                               : byte_string_view{single_child.data, single_child.len})),
                   node->is_leaf());
        }
    };
}

struct DummyComputeLeafData
{
    // TEMPORARY for POC
    // compute leaf data as - concat2(input_leaf, hash);
    static byte_string compute(Node const *const node)
    {
        return byte_string{node->value()} + byte_string{node->hash_view()};
    }
};

using MerkleCompute = detail::MerkleComputeBase<DummyComputeLeafData>;

class StateMachineWithBlockNo final : public TrieStateMachine
{
private:
    enum class TrieSection : uint8_t
    {
        BlockNo = 0,
        Account,
        Storage,
        Receipt, // not used yet
        Invalid
    }  default_section_, curr_section_;

    static std::pair<Compute &, Compute &> candidate_computes()
    {
        // candidate impls to use
        static MerkleCompute m{};
        static EmptyCompute e{};
        return {m, e};
    }

public:
    StateMachineWithBlockNo(uint8_t const sec = 0)
        : default_section_(static_cast<TrieSection>(sec))
        , curr_section_(default_section_)
    {
    }

    virtual std::unique_ptr<TrieStateMachine> clone() const override
    {
        return std::make_unique<StateMachineWithBlockNo>(
            static_cast<uint8_t>(default_section_));
    }

    virtual void reset(std::optional<uint8_t> sec) override
    {
        curr_section_ = sec.has_value() ? static_cast<TrieSection>(sec.value())
                                        : default_section_;
    }

    virtual void forward(byte_string_view = {}) override
    {
        switch (curr_section_) {
        case (TrieSection::BlockNo):
            curr_section_ = TrieSection::Account;
            break;
        case (TrieSection::Account):
            curr_section_ = TrieSection::Storage;
            break;
        default:
            curr_section_ = TrieSection::Invalid;
        }
    }

    virtual void backward() override
    {
        switch (curr_section_) {
        case (TrieSection::Storage):
            curr_section_ = TrieSection::Account;
            break;
        case (TrieSection::Account):
            curr_section_ = TrieSection::BlockNo;
            break;
        default:
            curr_section_ = TrieSection::Invalid;
        }
    }

    virtual constexpr Compute &get_compute() const override
    {
        if (curr_section_ == TrieSection::BlockNo) {
            return candidate_computes().second;
        }
        else {
            return candidate_computes().first;
        }
    }

    virtual constexpr uint8_t get_state() const override
    {
        return static_cast<uint8_t>(curr_section_);
    }

    virtual constexpr CacheOption cache_option() const override
    {
        switch (curr_section_) {
        case (TrieSection::BlockNo):
            return CacheOption::CacheAll;
        case (TrieSection::Account):
            return CacheOption::ApplyLevelBasedCache;
        default:
            return CacheOption::DisposeAll;
        }
    }
};
static_assert(sizeof(StateMachineWithBlockNo) == 16);
static_assert(alignof(StateMachineWithBlockNo) == 8);

MONAD_MPT_NAMESPACE_END
