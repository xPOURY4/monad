#pragma once

#include <monad/runtime/transmute.h>
#include <monad/runtime/types.h>
#include <monad/utils/uint256.h>

#include <evmc/evmc.hpp>

#include <limits>

namespace monad::runtime
{
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
        auto const &tx_context = ctx->env.tx_context;

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
    void selfbalance(Context *ctx, utils::uint256_t *result_ptr)
    {
        auto balance =
            ctx->host->get_balance(ctx->context, &ctx->env.recipient);
        *result_ptr = uint256_from_bytes32(balance);
    }

    template <evmc_revision Rev>
    void blobhash(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *index)
    {
        auto const &c = ctx->env.tx_context;
        *result_ptr = (*index < c.blob_hashes_count)
                          ? uint256_from_bytes32(
                                c.blob_hashes[static_cast<size_t>(*index)])
                          : 0;
    }
}
