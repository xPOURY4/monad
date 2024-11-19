#pragma once

#include <runtime/constants.h>
#include <runtime/exit.h>
#include <runtime/storage_costs.h>
#include <runtime/transmute.h>
#include <runtime/types.h>

#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <array>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void sload(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *key_ptr)
    {
        auto key = bytes_from_uint256(*key_ptr);

        auto access_status =
            ctx->host->access_storage(ctx->context, &ctx->env.recipient, &key);

        auto value =
            ctx->host->get_storage(ctx->context, &ctx->env.recipient, &key);

        auto gas_cost = load_base_gas(Rev);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                gas_cost += (COST_ACCESS_COLD - COST_ACCESS_WARM);
            }
        }

        ctx->gas_remaining -= gas_cost;

        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }

        *result_ptr = uint256_from_bytes32(value);
    }

    template <evmc_revision Rev>
    void sstore(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *key_ptr,
        utils::uint256_t const *value_ptr,
        std::int64_t remaining_block_base_gas)
    {
        if (ctx->env.evmc_flags == evmc_flags::EVMC_STATIC) {
            runtime_exit(
                exit_ctx->stack_pointer,
                exit_ctx->ctx,
                Error::StaticModeViolation);
        }

        if (ctx->gas_remaining + remaining_block_base_gas <= 2300) {
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }

        auto key = bytes_from_uint256(*key_ptr);
        auto value = bytes_from_uint256(*value_ptr);

        auto access_status =
            ctx->host->access_storage(ctx->context, &ctx->env.recipient, &key);

        auto storage_status = ctx->host->set_storage(
            ctx->context, &ctx->env.recipient, &key, &value);

        auto [gas_used, gas_refund] = store_cost<Rev>(storage_status);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                gas_used += COST_ACCESS_COLD;
            }
        }

        ctx->gas_refund += gas_refund;
        ctx->gas_remaining -= gas_used;

        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            runtime_exit(
                exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
        }
    }
}
