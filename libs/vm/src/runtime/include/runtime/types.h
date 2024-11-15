#pragma once

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <type_traits>
#include <vector>

namespace monad::runtime
{
    enum class Error : uint64_t
    {
        OutOfGas,
        StaticModeViolation,
        InvalidMemoryAccess,
    };

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

    static_assert(std::is_standard_layout_v<Result>);
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

    static_assert(std::is_standard_layout_v<Environment>);
    static_assert(sizeof(Environment) == 44);

    struct ExitContext;

    struct Context
    {
        static constexpr std::size_t max_memory_offset_bits = 24;

        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;
        std::vector<std::uint8_t> memory;
        std::uint64_t memory_cost;

        void expand_memory(ExitContext *exit_ctx, std::uint32_t size);

        utils::uint256_t mload(ExitContext *exit_ctx, utils::uint256_t offset);

    private:
        void set_memory_word(std::uint32_t offset, utils::uint256_t word);
        void set_memory_byte(std::uint32_t offset, std::uint8_t byte);
    };

    static_assert(std::is_standard_layout_v<Context>);
    static_assert(sizeof(Context) == 112);
    static_assert(offsetof(Context, host) == 0);
    static_assert(offsetof(Context, context) == 8);
    static_assert(offsetof(Context, gas_remaining) == 16);
    static_assert(offsetof(Context, gas_refund) == 24);
    static_assert(offsetof(Context, env) == 32);

    struct ExitContext
    {
        void *stack_pointer;
        Context *ctx;
    };

    static_assert(std::is_standard_layout_v<ExitContext>);
    static_assert(sizeof(ExitContext) == 16);
    static_assert(offsetof(ExitContext, stack_pointer) == 0);
    static_assert(offsetof(ExitContext, ctx) == 8);
}
