#pragma once

#include <monad/runtime/transmute.hpp>
#include <monad/runtime/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.hpp>

#include <ethash/keccak.hpp>

namespace monad::runtime
{
    inline void sha3(
        Context *ctx, vm::utils::uint256_t *result_ptr,
        vm::utils::uint256_t const *offset_ptr,
        vm::utils::uint256_t const *size_ptr)
    {
        Memory::Offset offset;
        auto size = ctx->get_memory_offset(*size_ptr);

        if (*size > 0) {
            offset = ctx->get_memory_offset(*offset_ptr);

            ctx->expand_memory(offset + size);

            auto word_size = shr_ceil<5>(size);
            ctx->deduct_gas(word_size * bin<6>);
        }

        auto hash = ethash::keccak256(ctx->memory.data + *offset, *size);
        *result_ptr = uint256_load_be(hash.bytes);
    }
}
