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

struct DbOptions;
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

        OnDisk();
    };

    std::optional<OnDisk> on_disk_;
    UpdateAux aux_;
    Node::UniquePtr root_;
    StateMachine &machine_;

public:
    Db(DbOptions const &);

    Result<byte_string_view> get(NibblesView);
    Result<byte_string_view> get_data(NibblesView);
    void upsert(UpdateList);
    void traverse(NibblesView root, TraverseMachine &);
};

MONAD_MPT_NAMESPACE_END
