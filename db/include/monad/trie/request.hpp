#pragma once

#include <monad/trie/config.hpp>
#include <monad/trie/util.hpp>

#include <monad/mpt/update.hpp>

#include <monad/mem/allocators.hpp>

#include <cassert>

#include <memory>

MONAD_TRIE_NAMESPACE_BEGIN

struct SubRequestInfo;
class merkle_node_t;

struct Request
{
    uint8_t pi;
    uint8_t prev_child_i;
    merkle_node_t *prev_parent;
    monad::mpt::UpdateList pending;

    using allocator_type = allocators::boost_unordered_pool_allocator<Request>;
    static allocator_type &pool()
    {
        static allocator_type v;
        return v;
    }
    using unique_ptr_type = std::unique_ptr<
        Request, allocators::unique_ptr_allocator_deleter<
                     allocator_type, &Request::pool>>;
    static unique_ptr_type
    make(monad::mpt::UpdateList &&updates, uint8_t path_len = 0)
    {
        return allocators::allocate_unique<allocator_type, &Request::pool>(
            Request(std::move(updates), path_len));
    }

private:
    Request(monad::mpt::UpdateList &&updates, uint8_t path_len)
        : pi(path_len)
        , pending(std::move(updates))
    {
    }

public:
    bool is_leaf()
    {
        return pending.size() == 1;
    }

    const monad::mpt::Update &get_only_leaf()
    {
        return pending.front();
    }

    const unsigned char *get_path()
    {
        return pending.front().key;
    }

    constexpr uint8_t path_len()
    {
        return pi;
    }

    unique_ptr_type split_into_subqueues(
        unique_ptr_type self, SubRequestInfo *subinfo, bool not_root = true);
};
static_assert(sizeof(Request) == 24);
static_assert(alignof(Request) == 8);

struct SubRequestInfo
{
    uint16_t mask{0};
    uint8_t path_len{0};
    allocators::owning_span<Request::unique_ptr_type> subqueues;

    constexpr SubRequestInfo() = default;

    const Request::unique_ptr_type &operator[](size_t i) const &
    {
        const auto idx = child_index(mask, i);
        assert(idx < subqueues.size());
        return subqueues[idx];
    }

    Request::unique_ptr_type &&operator[](size_t i) &&
    {
        const auto idx = child_index(mask, i);
        assert(idx < subqueues.size());
        return std::move(subqueues[idx]);
    }

    const unsigned char *get_path()
    {
        return subqueues[0]->get_path();
    }
};

static_assert(sizeof(SubRequestInfo) == 24);
static_assert(alignof(SubRequestInfo) == 8);

MONAD_TRIE_NAMESPACE_END