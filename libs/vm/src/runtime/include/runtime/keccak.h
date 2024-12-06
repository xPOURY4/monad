#pragma once

#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.hpp>

#include <ethash/keccak.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void sha3(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        auto [offset, size] =
            ctx->get_memory_offset_and_size(*offset_ptr, *size_ptr);

        if (size > 0) {
            ctx->expand_memory(size);

            auto word_size = (size + 31) / 32;
            ctx->deduct_gas(word_size * 6);
        }

        auto hash = ethash::keccak256(ctx->memory.data() + offset, size);
        *result_ptr = intx::be::load<utils::uint256_t>(hash.bytes);
    }
}
