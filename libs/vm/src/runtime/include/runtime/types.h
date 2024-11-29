#pragma once

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <span>
#include <type_traits>
#include <vector>

namespace monad::runtime
{
    enum class StatusCode : uint64_t
    {
        Success = 0,
        Revert,
        OutOfGas,
        StackOutOfBounds,
        StaticModeViolation,
        InvalidMemoryAccess,
        InvalidJump,
    };

    struct Result
    {
        uint8_t offset[32];
        uint8_t size[32];
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
        std::int32_t depth;
        evmc::address recipient;
        evmc::address sender;
        evmc::bytes32 value;
        evmc::bytes32 create2_salt;

        std::span<std::uint8_t const> input_data;
        std::span<std::uint8_t const> code;
        std::span<std::uint8_t const> return_data;

        void set_return_data(
            std::uint8_t const *output_data, std::uint32_t output_size);
        void clear_return_data();
    };

    static_assert(std::is_standard_layout_v<Environment>);
    static_assert(sizeof(Environment) == 160);

    struct Context
    {
        static constexpr std::size_t max_memory_offset_bits = 24;

        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;

        std::vector<std::uint8_t> memory = {};
        std::uint64_t memory_cost = 0;

        void *exit_stack_ptr = nullptr;

        void deduct_gas(std::int64_t gas);

        void expand_memory(std::uint32_t size);
        void expand_memory_unchecked(std::uint32_t size);

        utils::uint256_t mload(utils::uint256_t offset);

        void mstore(utils::uint256_t offset_word, utils::uint256_t value);

        void mstore8(utils::uint256_t offset_word, utils::uint256_t value);

        utils::uint256_t msize() const;

        std::uint32_t get_memory_offset(utils::uint256_t offset);

        std::pair<std::uint32_t, std::uint32_t> get_memory_offset_and_size(
            utils::uint256_t offset, utils::uint256_t size);

        evmc_tx_context get_tx_context() const;

        void exit [[noreturn]] (StatusCode code) const noexcept;

    private:
        void set_memory_word(std::uint32_t offset, utils::uint256_t word);
        void set_memory_byte(std::uint32_t offset, std::uint8_t byte);
    };

    static_assert(std::is_standard_layout_v<Context>);
    static_assert(sizeof(Context) == 232);
    static_assert(offsetof(Context, host) == 0);
    static_assert(offsetof(Context, context) == 8);
    static_assert(offsetof(Context, gas_remaining) == 16);
    static_assert(offsetof(Context, gas_refund) == 24);
    static_assert(offsetof(Context, env) == 32);
    static_assert(offsetof(Context, exit_stack_ptr) == 224);
}
