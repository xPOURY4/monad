#pragma once

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
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *key_ptr)
    {
        auto key = bytes32_from_uint256(*key_ptr);

        auto access_status =
            ctx->host->access_storage(ctx->context, &ctx->env.recipient, &key);

        auto value =
            ctx->host->get_storage(ctx->context, &ctx->env.recipient, &key);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->deduct_gas(2000);
            }
        }

        *result_ptr = uint256_from_bytes32(value);
    }

    template <evmc_revision Rev>
    void sstore(
        Context *ctx, utils::uint256_t const *key_ptr,
        utils::uint256_t const *value_ptr,
        std::int64_t remaining_block_base_gas)
    {
        if (ctx->env.evmc_flags == evmc_flags::EVMC_STATIC) {
            ctx->exit(StatusCode::StaticModeViolation);
        }

        // EIP-2200
        if constexpr (Rev >= EVMC_ISTANBUL) {
            if (ctx->gas_remaining + remaining_block_base_gas <= 2300) {
                ctx->exit(StatusCode::OutOfGas);
            }
        }

        auto key = bytes32_from_uint256(*key_ptr);
        auto value = bytes32_from_uint256(*value_ptr);

        auto access_status =
            ctx->host->access_storage(ctx->context, &ctx->env.recipient, &key);

        auto storage_status = ctx->host->set_storage(
            ctx->context, &ctx->env.recipient, &key, &value);

        auto [gas_used, gas_refund] = store_cost<Rev>(storage_status);

        // The code generator has taken care of accounting for the minimum base
        // gas cost of this SSTORE already, but to keep the table of costs
        // readable it encodes the total gas usage of each combination, rather
        // than the amount relative to the minimum gas.
        gas_used -= minimum_store_gas<Rev>();

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                gas_used += 2100;
            }
        }

        ctx->gas_refund += gas_refund;
        ctx->deduct_gas(gas_used);
    }

    template <evmc_revision Rev>
    void tload(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *key_ptr)
    {
        MONAD_COMPILER_DEBUG_ASSERT(Rev >= EVMC_CANCUN);

        auto key = bytes32_from_uint256(*key_ptr);

        auto value = ctx->host->get_transient_storage(
            ctx->context, &ctx->env.recipient, &key);

        *result_ptr = uint256_from_bytes32(value);
    }

    template <evmc_revision Rev>
    void tstore(
        Context *ctx, utils::uint256_t const *key_ptr,
        utils::uint256_t const *val_ptr)
    {
        MONAD_COMPILER_DEBUG_ASSERT(Rev >= EVMC_CANCUN);

        auto key = bytes32_from_uint256(*key_ptr);
        auto val = bytes32_from_uint256(*val_ptr);

        ctx->host->set_transient_storage(
            ctx->context, &ctx->env.recipient, &key, &val);
    }
}
