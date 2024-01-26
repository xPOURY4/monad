#pragma once

#include <monad/async/io.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/result.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>

MONAD_MPT_NAMESPACE_BEGIN

struct OnDiskDbConfig;
struct StateMachine;
struct TraverseMachine;

class Db
{
private:
    struct OnDisk
    {
        async::storage_pool pool;
        io::Ring ring;
        io::Buffers rwbuf;
        async::AsyncIO io;
        bool compaction;

        OnDisk(OnDiskDbConfig const &);
    };

    std::optional<OnDisk> on_disk_;
    UpdateAux aux_;
    Node::UniquePtr root_;
    StateMachine &machine_;

public:
    //! construct an in memory db
    Db(StateMachine &);
    //! construct an on disk db
    Db(StateMachine &, OnDiskDbConfig const &);

    Result<byte_string_view> get(NibblesView);
    Result<byte_string_view> get_data(NibblesView);
    void upsert(UpdateList, uint64_t block_id = 0);
    void traverse(NibblesView root, TraverseMachine &);
};

MONAD_MPT_NAMESPACE_END
