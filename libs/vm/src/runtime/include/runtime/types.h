#pragma once

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    enum class Error : uint64_t
    {
        OutOfGas,
        StaticModeViolation,
        InvalidMemoryAccess,
    };

    using RuntimeExit = void (*)(Error);

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

    struct Environment
    {
        std::uint32_t evmc_flags;
        evmc::address recipient;
        evmc::address sender;
    };

    struct Context
    {
        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;
    };
}
