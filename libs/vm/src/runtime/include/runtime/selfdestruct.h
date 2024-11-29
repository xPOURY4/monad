#pragma once

#include <runtime/exit.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void selfdestruct(
        ExitContext *exit_ctx, Context *ctx,
        utils::uint256_t const *address_ptr)
    {
        if (ctx->env.evmc_flags == EVMC_STATIC) {
            exit_ctx->exit(StatusCode::StaticModeViolation);
        }

        if constexpr (Rev >= EVMC_TANGERINE_WHISTLE) {
            ctx->gas_remaining -= 5000;
            if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
                exit_ctx->exit(StatusCode::OutOfGas);
            }
        }

        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->gas_remaining -= 2600;
            }
        }

        auto balance =
            ctx->host->get_balance(ctx->context, &ctx->env.recipient);

        if constexpr (Rev >= EVMC_TANGERINE_WHISTLE) {
            auto non_zero_transfer = [&balance] {
                if constexpr (Rev == EVMC_TANGERINE_WHISTLE) {
                    return true;
                }

                return balance != evmc::bytes32{};
            }();

            if (non_zero_transfer) {
                auto exists = ctx->host->account_exists(ctx->context, &address);

                if (!exists) {
                    ctx->gas_remaining -= 25000;
                }
            }
        }

        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            exit_ctx->exit(StatusCode::OutOfGas);
        }

        auto result = ctx->host->selfdestruct(
            ctx->context, &ctx->env.recipient, &address);

        if constexpr (Rev < EVMC_LONDON) {
            if (result) {
                ctx->gas_refund += 24000;
            }
        }
    }
}
