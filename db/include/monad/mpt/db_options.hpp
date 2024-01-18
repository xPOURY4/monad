#pragma once

#include <monad/mpt/config.hpp>

MONAD_MPT_NAMESPACE_BEGIN

struct StateMachine;

struct DbOptions
{
    bool on_disk{false};
    bool append{false};
    unsigned rd_buffers{2};
    unsigned wr_buffers{4};
    unsigned uring_entries{2};
    unsigned sq_thread_cpu{0};
    std::vector<std::filesystem::path> dbname_paths{};
};

MONAD_MPT_NAMESPACE_END
