#pragma once

#include <runtime/exit.h>
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
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t *const offset_ptr, utils::uint256_t *const size_ptr)
    {
        auto [offset, size] = Context::get_memory_offset_and_size(
            exit_ctx, *offset_ptr, *size_ptr);

        if (size > 0) {
            ctx->expand_memory(exit_ctx, size);

            auto word_size = (size + 31) / 32;

            ctx->gas_remaining -= word_size * 6;
            if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
                runtime_exit(
                    exit_ctx->stack_pointer,
                    exit_ctx->ctx,
                    StatusCode::OutOfGas);
            }
        }

        auto hash = ethash::keccak256(ctx->memory.data() + offset, size);
        *result_ptr = intx::be::load<utils::uint256_t>(hash.bytes);
    }
}
