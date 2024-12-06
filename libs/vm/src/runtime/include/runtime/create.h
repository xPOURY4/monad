#pragma once

#include <runtime/arithmetic.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    consteval std::int64_t create_code_word_cost(evmc_revision rev)
    {
        return (rev >= EVMC_SHANGHAI) ? 2 : 0;
    }

    consteval std::int64_t create2_code_word_cost(evmc_revision rev)
    {
        return (rev >= EVMC_SHANGHAI) ? 8 : 6;
    }

    template <evmc_revision Rev>
    utils::uint256_t create_impl(
        Context *ctx, utils::uint256_t value, utils::uint256_t offset_word,
        utils::uint256_t size_word, utils::uint256_t salt_word,
        evmc_call_kind kind, std::int64_t remaining_block_base_gas)
    {
        if (MONAD_COMPILER_UNLIKELY(ctx->env.evmc_flags == EVMC_STATIC)) {
            ctx->exit(StatusCode::StaticModeViolation);
        }

        ctx->env.clear_return_data();

        auto [offset, size] =
            ctx->get_memory_offset_and_size(offset_word, size_word);

        if (size > 0) {
            ctx->expand_memory(saturating_add(offset, size));
        }

        if constexpr (Rev >= EVMC_SHANGHAI) {
            if (MONAD_COMPILER_UNLIKELY(size > 0xC000)) {
                ctx->exit(StatusCode::OutOfGas);
            }
        }

        auto min_words = (size + 31) / 32;
        auto word_cost = (kind == EVMC_CREATE2) ? create2_code_word_cost(Rev)
                                                : create_code_word_cost(Rev);

        ctx->deduct_gas(min_words * word_cost);

        if (MONAD_COMPILER_UNLIKELY(ctx->env.depth >= 1024)) {
            return 0;
        }

        if (value != 0) {
            auto balance = uint256_from_bytes32(
                ctx->host->get_balance(ctx->context, &ctx->env.recipient));

            if (MONAD_COMPILER_UNLIKELY(balance < value)) {
                ctx->exit(StatusCode::OutOfGas);
            }
        }

        auto gas_left_here = ctx->gas_remaining + remaining_block_base_gas;
        auto gas = gas_left_here;

        if constexpr (Rev >= EVMC_TANGERINE_WHISTLE) {
            gas = gas_left_here - (gas_left_here / 64);
        }

        auto message = evmc_message{
            .kind = kind,
            .flags = ctx->env.evmc_flags,
            .depth = ctx->env.depth + 1,
            .gas = gas,
            .recipient = evmc::address{},
            .sender = ctx->env.recipient,
            .input_data = (size > 0) ? ctx->memory.data() + offset : nullptr,
            .input_size = size,
            .value = bytes_from_uint256(value),
            .create2_salt = bytes_from_uint256(salt_word),
            .code_address = evmc::address{},
            .code = nullptr,
            .code_size = 0,
        };

        auto result = ctx->host->call(ctx->context, &message);
        auto call_gas_used = gas - result.gas_left;

        ctx->gas_refund += result.gas_refund;

        if (MONAD_COMPILER_UNLIKELY(
                result.output_size >
                std::numeric_limits<std::uint32_t>::max())) {
            ctx->exit(StatusCode::OutOfGas);
        }

        ctx->deduct_gas(call_gas_used);

        ctx->env.set_return_data(
            result.output_data, static_cast<std::uint32_t>(result.output_size));

        return (result.status_code == EVMC_SUCCESS)
                   ? uint256_from_address(result.create_address)
                   : 0;
    }

    template <evmc_revision Rev>
    void create(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t *const value_ptr, utils::uint256_t *const offset_ptr,
        utils::uint256_t *const size_ptr, std::int64_t remaining_block_base_gas)
    {
        *result_ptr = create_impl<Rev>(
            ctx,
            *value_ptr,
            *offset_ptr,
            *size_ptr,
            0,
            EVMC_CREATE,
            remaining_block_base_gas);
    }

    template <evmc_revision Rev>
    void create2(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t *const value_ptr, utils::uint256_t *const offset_ptr,
        utils::uint256_t *const size_ptr, utils::uint256_t *const salt_ptr,
        std::int64_t remaining_block_base_gas)
    {
        *result_ptr = create_impl<Rev>(
            ctx,
            *value_ptr,
            *offset_ptr,
            *size_ptr,
            *salt_ptr,
            EVMC_CREATE2,
            remaining_block_base_gas);
    }
}
