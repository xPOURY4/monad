#pragma once

#include <monad/trie/util.hpp>

#include <monad/mpt/update.hpp>

#include <boost/pool/object_pool.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

struct SubRequestInfo;
struct merkle_node_t;

struct Request
{
    uint8_t pi;
    uint8_t prev_child_i;
    merkle_node_t *prev_parent;
    monad::mpt::UpdateList pending;
    static inline boost::object_pool<Request> pool{};

    Request(monad::mpt::UpdateList &updates, uint8_t path_len = 0)
        : pi(path_len)
        , pending(std::move(updates))
    {
    }

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

    Request *
    split_into_subqueues(SubRequestInfo *subinfo, bool not_root = true);
};
static_assert(sizeof(Request) == 24);
static_assert(alignof(Request) == 8);

struct SubRequestInfo
{
    uint16_t mask;
    uint8_t path_len;
    Request **subqueues;

    constexpr SubRequestInfo()
        : mask(0)
        , subqueues(nullptr)
    {
    }

    ~SubRequestInfo()
    {
        if (subqueues) {
            free(subqueues);
        }
    }

    Request *operator[](int i)
    {
        return subqueues[child_index(mask, i)];
    }

    const unsigned char *get_path()
    {
        return subqueues[0]->get_path();
    }
};

static_assert(sizeof(SubRequestInfo) == 16);
static_assert(alignof(SubRequestInfo) == 8);

MONAD_TRIE_NAMESPACE_END