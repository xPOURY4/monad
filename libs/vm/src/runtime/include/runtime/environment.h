#pragma once

#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <limits>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void origin(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_address(ctx->get_tx_context().tx_origin);
    }

    template <evmc_revision Rev>
    void gasprice(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_bytes32(ctx->get_tx_context().tx_gas_price);
    }

    template <evmc_revision Rev>
    void gaslimit(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_gas_limit;
    }

    template <evmc_revision Rev>
    void blockhash(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *block_number_ptr)
    {
        if (!is_bounded_by_bits<63>(*block_number_ptr)) {
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
    void coinbase(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_address(ctx->get_tx_context().block_coinbase);
    }

    template <evmc_revision Rev>
    void timestamp(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_timestamp;
    }

    template <evmc_revision Rev>
    void number(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = ctx->get_tx_context().block_number;
    }

    template <evmc_revision Rev>
    void prevrandao(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_bytes32(ctx->get_tx_context().block_prev_randao);
    }

    template <evmc_revision Rev>
    void chainid(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_bytes32(ctx->get_tx_context().chain_id);
    }

    template <evmc_revision Rev>
    void selfbalance(Context *ctx, utils::uint256_t *result_ptr)
    {
        auto balance =
            ctx->host->get_balance(ctx->context, &ctx->env.recipient);
        *result_ptr = uint256_from_bytes32(balance);
    }

    template <evmc_revision Rev>
    void basefee(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr =
            uint256_from_bytes32(ctx->get_tx_context().block_base_fee);
    }

    template <evmc_revision Rev>
    void blobhash(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *index)
    {
        auto const &c = ctx->get_tx_context();
        *result_ptr = (*index < c.blob_hashes_count)
                          ? uint256_from_bytes32(
                                c.blob_hashes[static_cast<size_t>(*index)])
                          : 0;
    }

    template <evmc_revision Rev>
    void blobbasefee(Context *ctx, utils::uint256_t *result_ptr)
    {
        *result_ptr = uint256_from_bytes32(ctx->get_tx_context().blob_base_fee);
    }
}
