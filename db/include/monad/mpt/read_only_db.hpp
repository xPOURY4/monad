#pragma once

#include <monad/async/io.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/result.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>

MONAD_MPT_NAMESPACE_BEGIN

struct ReadOnlyOnDiskDbConfig
{
    bool disable_mismatching_storage_pool_check{
        false}; // risk of severe data loss
    bool capture_io_latencies{false};
    bool eager_completions{false};
    unsigned rd_buffers{8};
    unsigned uring_entries{8};
    // default to disable sqpoll kernel thread since now ReadOnlyDb uses
    // blocking read
    std::optional<unsigned> sq_thread_cpu{std::nullopt};
    std::vector<std::filesystem::path> dbname_paths;
    unsigned concurrent_read_io_limit{1024};
};

class ReadOnlyDb
{
    async::storage_pool pool_;
    io::Ring ring_;
    io::Buffers rwbuf_;
    async::AsyncIO io_;
    UpdateAux<> aux_;
    MONAD_ASYNC_NAMESPACE::chunk_offset_t last_loaded_offset_;
    Node::UniquePtr root_;

public:
    ReadOnlyDb(ReadOnlyOnDiskDbConfig const &);

    //! True if this DB is the latest DB (fast)
    bool is_latest() const;
    //! Load the latest DB root
    void load_latest();

    //! Temporarily implemented as blocking find
    Result<byte_string_view> get(NibblesView, uint64_t block_id = 0) const;
    //! Temporarily implemented as blocking find
    Result<byte_string_view> get_data(NibblesView, uint64_t block_id = 0) const;
    //! Temporarily implemented as blocking find
    Result<NodeCursor> get(NodeCursor, NibblesView) const;
    //! Temporarily implemented as blocking find
    Result<byte_string_view> get_data(NodeCursor, NibblesView) const;

    //! Returns the root for this DB
    NodeCursor root() const noexcept
    {
        return root_ ? NodeCursor{*root_} : NodeCursor{};
    }
};

MONAD_MPT_NAMESPACE_END
