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
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t gas_word,
        utils::uint256_t address, bool has_value, evmc_bytes32 value,
        utils::uint256_t args_offset_word, utils::uint256_t args_size_word,
        utils::uint256_t ret_offset_word, utils::uint256_t ret_size_word,
        evmc_call_kind call_kind, bool static_call,
        std::int64_t remaining_block_base_gas)
    {
        ctx->gas_remaining -= call_base_gas(Rev);
        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }

        ctx->env.clear_return_data();

        auto [args_offset, args_size] = Context::get_memory_offset_and_size(
            exit_ctx, args_offset_word, args_size_word);

        auto [ret_offset, ret_size] = Context::get_memory_offset_and_size(
            exit_ctx, ret_offset_word, ret_size_word);

        if (args_size > 0) {
            ctx->expand_memory(
                exit_ctx, saturating_add(args_offset, args_size));
        }

        if (ret_size > 0) {
            ctx->expand_memory(exit_ctx, saturating_add(ret_offset, ret_size));
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
                runtime_exit(
                    exit_ctx->stack_pointer,
                    exit_ctx->ctx,
                    Error::StaticModeViolation);
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
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }

        auto i64_max = std::numeric_limits<std::int64_t>::max();
        auto gas = (gas_word > i64_max) ? i64_max
                                        : static_cast<std::int64_t>(gas_word);

        if constexpr (Rev >= EVMC_TANGERINE_WHISTLE) {
            gas = std::min(gas, gas_left_here - (gas_left_here / 64));
        }
        else {
            if (MONAD_COMPILER_UNLIKELY(gas > gas_left_here)) {
                runtime_exit(
                    exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
            }
        }

        if (has_value) {
            gas += 2300;
            ctx->gas_remaining -= 2300;

            if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
                runtime_exit(
                    exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
            }
        }

        if (ctx->env.depth >= 1024) {
            return 0;
        }

        auto message = evmc_message{
            .kind = call_kind,
            .flags = static_call ? EVMC_STATIC : ctx->env.evmc_flags,
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
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }

        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
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
}
