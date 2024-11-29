#pragma once

#include <runtime/constants.h>
#include <runtime/exit.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void balance(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *address_ptr)
    {
        ctx->gas_remaining -= balance_base_gas(Rev);
        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            exit_ctx->exit(StatusCode::OutOfGas);
        }

        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->gas_remaining -= 2500;
                if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
                    exit_ctx->exit(StatusCode::OutOfGas);
                }
            }
        }

        auto balance = ctx->host->get_balance(ctx->context, &address);
        *result_ptr = uint256_from_bytes32(balance);
    }
}
