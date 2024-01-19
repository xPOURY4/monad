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
    /* optional max_block_id to resume db commit from, if not contain any value
    then will restore from the actual max_block_id in on disk db */
    std::optional<uint64_t> opt_max_block_id{std::nullopt};
    std::vector<std::filesystem::path> dbname_paths{};
};

MONAD_MPT_NAMESPACE_END
