#pragma once

#include <evmc/evmc.h>

namespace monad::runtime
{
    enum class StatusCode : uint64_t
    {
        success = 0,
    };

    struct Result
    {
        evmc_bytes32 offset;
        evmc_bytes32 size;
        StatusCode status;
    };

    static_assert(sizeof(Result) == 72);
    static_assert(offsetof(Result, offset) == 0);
    static_assert(offsetof(Result, size) == 32);
    static_assert(offsetof(Result, status) == 64);

    struct Context
    {
        evmc_host_interface const *host;
        evmc_host_context *context;
    };
}
