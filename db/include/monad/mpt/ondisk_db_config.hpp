#pragma once

#include <monad/mpt/config.hpp>

#include <filesystem>

MONAD_MPT_NAMESPACE_BEGIN

struct StateMachine;

struct OnDiskDbConfig
{
    bool append{false};
    bool compaction{false};
    unsigned rd_buffers{2};
    unsigned wr_buffers{4};
    unsigned uring_entries{2};
    unsigned sq_thread_cpu{0};
    std::optional<uint64_t> start_block_id{std::nullopt};
    std::vector<std::filesystem::path> dbname_paths{};
    int64_t file_size_db{512}; // truncate files to this size
};

MONAD_MPT_NAMESPACE_END
