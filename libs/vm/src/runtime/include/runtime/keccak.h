#pragma once

#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <ethash/keccak.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void sha3(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        std::uint32_t offset;
        auto size = ctx->get_memory_offset(*size_ptr);

        if (size > 0) {
            offset = ctx->get_memory_offset(*offset_ptr);

            ctx->expand_memory<false>(offset + size);

            auto word_size = (size + 31) / 32;
            ctx->deduct_gas(word_size * 6);
        }
        else {
            offset = 0;
        }

        auto hash = ethash::keccak256(ctx->memory.data + offset, size);
        *result_ptr = uint256_load_be(hash.bytes);
    }
}
