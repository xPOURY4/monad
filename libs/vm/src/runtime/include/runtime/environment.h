#pragma once

#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <limits>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void address(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_address(ctx->env.recipient);
    }

    template <evmc_revision Rev>
    void origin(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_address(ctx->get_tx_context().tx_origin);
    }

    template <evmc_revision Rev>
    void gasprice(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_bytes32(ctx->get_tx_context().tx_gas_price);
    }

    template <evmc_revision Rev>
    void gaslimit(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_gas_limit;
    }

    template <evmc_revision Rev>
    void blockhash(
        ExitContext *, Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *block_number_ptr)
    {
        if (*block_number_ptr > std::numeric_limits<std::uint64_t>::max()) {
            *result_ptr = 0;
            return;
        }

        auto block_number = static_cast<std::int64_t>(*block_number_ptr);
        auto tx_context = ctx->get_tx_context();

        auto first_allowed_block = std::max(tx_context.block_number - 256, 0L);
        if (block_number >= first_allowed_block &&
            block_number < tx_context.block_number) {
            auto hash = ctx->host->get_block_hash(ctx->context, block_number);
            *result_ptr = uint256_from_bytes32(hash);
        }
        else {
            *result_ptr = 0;
        }
    }

    template <evmc_revision Rev>
    void coinbase(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_address(ctx->get_tx_context().block_coinbase);
    }

    template <evmc_revision Rev>
    void timestamp(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_timestamp;
    }

    template <evmc_revision Rev>
    void number(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_number;
    }

    template <evmc_revision Rev>
    void prevrandao(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_bytes32(ctx->get_tx_context().block_prev_randao);
    }

    template <evmc_revision Rev>
    void chainid(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_bytes32(ctx->get_tx_context().chain_id);
    }

    template <evmc_revision Rev>
    void selfbalance(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        auto balance =
            ctx->host->get_balance(ctx->context, &ctx->env.recipient);
        *result_ptr = uint256_from_bytes32(balance);
    }

    template <evmc_revision Rev>
    void basefee(ExitContext *, Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_bytes32(ctx->get_tx_context().block_base_fee);
    }
}
