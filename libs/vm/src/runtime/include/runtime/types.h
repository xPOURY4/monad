#pragma once

#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

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
        InvalidInstruction,
    };

    struct alignas(uint64_t) Result
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

        std::uint8_t const *input_data;
        std::uint8_t const *code;
        std::uint8_t const *return_data;

        std::uint32_t input_data_size;
        std::uint32_t code_size;
        std::uint32_t return_data_size;

        void set_return_data(
            std::uint8_t const *output_data, std::uint32_t output_size)
        {
            MONAD_COMPILER_ASSERT(return_data_size == 0);
            return_data = output_data;
            return_data_size = output_size;
        }

        [[gnu::always_inline]]
        void clear_return_data()
        {
            return_data_size = 0;
        }
    };

    static_assert(std::is_standard_layout_v<Environment>);
    static_assert(sizeof(Environment) == 152);

    struct Context
    {
        static constexpr std::size_t max_memory_offset_bits = 24;

        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;

        Result result = {};

        std::vector<std::uint8_t> memory = {};
        std::uint32_t memory_size = 0;
        std::uint64_t memory_cost = 0;

        void *exit_stack_ptr = nullptr;

        [[gnu::always_inline]]
        void deduct_gas(std::int64_t gas)
        {
            gas_remaining -= gas;
            if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
                exit(StatusCode::OutOfGas);
            }
        }

        void expand_memory(std::uint32_t size);
        void expand_memory_unchecked(std::uint32_t size);

        utils::uint256_t mload(utils::uint256_t offset);

        void mstore(utils::uint256_t offset_word, utils::uint256_t value);

        void mstore8(utils::uint256_t offset_word, utils::uint256_t value);

        void mcopy(
            utils::uint256_t dst_in, utils::uint256_t src_in,
            utils::uint256_t size_in);

        std::uint32_t get_memory_offset(utils::uint256_t offset);

        std::pair<std::uint32_t, std::uint32_t> get_memory_offset_and_size(
            utils::uint256_t offset, utils::uint256_t size);

        [[gnu::always_inline]]
        evmc_tx_context get_tx_context() const
        {
            return host->get_tx_context(context);
        }

        void exit [[noreturn]] (StatusCode code) noexcept;

    private:
        void set_memory_word(std::uint32_t offset, utils::uint256_t word);
        void set_memory_byte(std::uint32_t offset, std::uint8_t byte);
    };

    static_assert(std::is_standard_layout_v<Context>);
    static_assert(sizeof(Context) == 304);
    static_assert(offsetof(Context, host) == 0);
    static_assert(offsetof(Context, context) == 8);
    static_assert(offsetof(Context, gas_remaining) == 16);
    static_assert(offsetof(Context, gas_refund) == 24);
    static_assert(offsetof(Context, env) == 32);
    static_assert(offsetof(Context, result) == 184);
    static_assert(offsetof(Context, exit_stack_ptr) == 296);
}
