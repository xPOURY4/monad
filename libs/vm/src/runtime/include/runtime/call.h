#pragma once

#include <runtime/arithmetic.h>
#include <runtime/constants.h>
#include <runtime/exit.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    utils::uint256_t call_impl(
        Context *ctx, utils::uint256_t gas_word, utils::uint256_t address,
        bool has_value, evmc_bytes32 value, utils::uint256_t args_offset_word,
        utils::uint256_t args_size_word, utils::uint256_t ret_offset_word,
        utils::uint256_t ret_size_word, evmc_call_kind call_kind,
        bool static_call, std::int64_t remaining_block_base_gas)
    {
        ctx->gas_remaining -= call_base_gas(Rev);
        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            ctx->exit(StatusCode::OutOfGas);
        }

        ctx->env.clear_return_data();

        auto [args_offset, args_size] =
            ctx->get_memory_offset_and_size(args_offset_word, args_size_word);

        auto [ret_offset, ret_size] =
            ctx->get_memory_offset_and_size(ret_offset_word, ret_size_word);

        if (args_size > 0) {
            ctx->expand_memory(saturating_add(args_offset, args_size));
        }

        if (ret_size > 0) {
            ctx->expand_memory(saturating_add(ret_offset, ret_size));
        }

        auto code_address = address_from_uint256(address);

        auto access_status =
            ctx->host->access_account(ctx->context, &code_address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->gas_remaining -= 2500;
            }
        }

        auto recipient = (call_kind == EVMC_CALL || static_call)
                             ? code_address
                             : ctx->env.recipient;

        auto sender = (call_kind == EVMC_DELEGATECALL) ? ctx->env.sender
                                                       : ctx->env.recipient;

        if (has_value) {
            ctx->gas_remaining -= 9000;
        }

        if (call_kind == EVMC_CALL) {
            if (MONAD_COMPILER_UNLIKELY(
                    has_value && ctx->env.evmc_flags == EVMC_STATIC)) {
                ctx->exit(StatusCode::StaticModeViolation);
            }

            auto empty_account = false;
            if constexpr (Rev < EVMC_SPURIOUS_DRAGON) {
                empty_account =
                    ctx->host->account_exists(ctx->context, &code_address);
            }

            if (has_value || empty_account) {
                ctx->gas_remaining -= 25000;
            }
        }

        auto gas_left_here = ctx->gas_remaining + remaining_block_base_gas;

        if (MONAD_COMPILER_UNLIKELY(gas_left_here < 0)) {
            ctx->exit(StatusCode::OutOfGas);
        }

        auto i64_max = std::numeric_limits<std::int64_t>::max();
        auto gas = (gas_word > i64_max) ? i64_max
                                        : static_cast<std::int64_t>(gas_word);

        if constexpr (Rev >= EVMC_TANGERINE_WHISTLE) {
            gas = std::min(gas, gas_left_here - (gas_left_here / 64));
        }
        else {
            if (MONAD_COMPILER_UNLIKELY(gas > gas_left_here)) {
                ctx->exit(StatusCode::OutOfGas);
            }
        }

        if (has_value) {
            gas += 2300;
            ctx->gas_remaining -= 2300;

            if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
                ctx->exit(StatusCode::OutOfGas);
            }
        }

        if (ctx->env.depth >= 1024) {
            return 0;
        }

        auto message = evmc_message{
            .kind = call_kind,
            .flags = static_call ? static_cast<std::uint32_t>(EVMC_STATIC)
                                 : ctx->env.evmc_flags,
            .depth = ctx->env.depth + 1,
            .gas = gas,
            .recipient = recipient,
            .sender = sender,
            .input_data =
                (args_size > 0) ? ctx->memory.data() + args_offset : nullptr,
            .input_size = args_size,
            .value = value,
            .create2_salt = ctx->env.create2_salt,
            .code_address = code_address,
            .code = nullptr,
            .code_size = 0,
        };

        auto result = ctx->host->call(ctx->context, &message);
        auto call_gas_used = gas - result.gas_left;

        ctx->gas_refund += result.gas_refund;
        ctx->gas_remaining -= call_gas_used;

        if (MONAD_COMPILER_UNLIKELY(
                result.output_size >
                std::numeric_limits<std::uint32_t>::max())) {
            ctx->exit(StatusCode::OutOfGas);
        }

        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            ctx->exit(StatusCode::OutOfGas);
        }

        ctx->env.set_return_data(
            result.output_data, static_cast<std::uint32_t>(result.output_size));

        auto copy_size =
            std::min(static_cast<std::size_t>(ret_size), result.output_size);

        if (copy_size > 0) {
            std::copy(
                result.output_data,
                result.output_data + copy_size,
                ctx->memory.begin() + ret_offset);
        }

        return (result.status_code == EVMC_SUCCESS) ? 1 : 0;
    }

    template <evmc_revision Rev>
    void call(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *gas_ptr, utils::uint256_t const *address_ptr,
        utils::uint256_t const *value_ptr,
        utils::uint256_t const *args_offset_ptr,
        utils::uint256_t const *args_size_ptr,
        utils::uint256_t const *ret_offset_ptr,
        utils::uint256_t const *ret_size_ptr,
        std::int64_t remaining_block_base_gas)
    {
        *result_ptr = call_impl<Rev>(
            ctx,
            *gas_ptr,
            *address_ptr,
            *value_ptr != 0,
            bytes_from_uint256(*value_ptr),
            *args_offset_ptr,
            *args_size_ptr,
            *ret_offset_ptr,
            *ret_size_ptr,
            EVMC_CALL,
            false,
            remaining_block_base_gas);
    }

    template <evmc_revision Rev>
    void callcode(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *gas_ptr, utils::uint256_t const *address_ptr,
        utils::uint256_t const *value_ptr,
        utils::uint256_t const *args_offset_ptr,
        utils::uint256_t const *args_size_ptr,
        utils::uint256_t const *ret_offset_ptr,
        utils::uint256_t const *ret_size_ptr,
        std::int64_t remaining_block_base_gas)
    {
        *result_ptr = call_impl<Rev>(
            ctx,
            *gas_ptr,
            *address_ptr,
            *value_ptr != 0,
            bytes_from_uint256(*value_ptr),
            *args_offset_ptr,
            *args_size_ptr,
            *ret_offset_ptr,
            *ret_size_ptr,
            EVMC_CALLCODE,
            false,
            remaining_block_base_gas);
    }

    template <evmc_revision Rev>
    void delegatecall(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *gas_ptr, utils::uint256_t const *address_ptr,
        utils::uint256_t const *args_offset_ptr,
        utils::uint256_t const *args_size_ptr,
        utils::uint256_t const *ret_offset_ptr,
        utils::uint256_t const *ret_size_ptr,
        std::int64_t remaining_block_base_gas)
    {
        *result_ptr = call_impl<Rev>(
            ctx,
            *gas_ptr,
            *address_ptr,
            false,
            ctx->env.value,
            *args_offset_ptr,
            *args_size_ptr,
            *ret_offset_ptr,
            *ret_size_ptr,
            EVMC_DELEGATECALL,
            false,
            remaining_block_base_gas);
    }

    template <evmc_revision Rev>
    void staticcall(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *gas_ptr, utils::uint256_t const *address_ptr,
        utils::uint256_t const *args_offset_ptr,
        utils::uint256_t const *args_size_ptr,
        utils::uint256_t const *ret_offset_ptr,
        utils::uint256_t const *ret_size_ptr,
        std::int64_t remaining_block_base_gas)
    {
        *result_ptr = call_impl<Rev>(
            ctx,
            *gas_ptr,
            *address_ptr,
            false,
            evmc::bytes32{},
            *args_offset_ptr,
            *args_size_ptr,
            *ret_offset_ptr,
            *ret_size_ptr,
            EVMC_CALL,
            true,
            remaining_block_base_gas);
    }
}
