#pragma once

#include <runtime/exit.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/uint256.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void log_impl(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t offset_word,
        utils::uint256_t size_word, std::span<evmc::bytes32 const> topics)
    {
        if (ctx->env.evmc_flags == EVMC_STATIC) {
            exit_ctx->exit(StatusCode::StaticModeViolation);
        }

        auto [offset, size] = Context::get_memory_offset_and_size(
            exit_ctx, offset_word, size_word);

        if (size > 0) {
            ctx->expand_memory(exit_ctx, size);
        }

        ctx->host->emit_log(
            ctx->context,
            &ctx->env.recipient,
            ctx->memory.data() + offset,
            size,
            topics.data(),
            topics.size());
    }

    template <evmc_revision Rev>
    void log0(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t *const size_ptr)
    {
        log_impl<Rev>(exit_ctx, ctx, *offset_ptr, *size_ptr, {});
    }

    template <evmc_revision Rev>
    void log1(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t *const size_ptr, utils::uint256_t *const topic1_ptr)
    {
        log_impl<Rev>(
            exit_ctx,
            ctx,
            *offset_ptr,
            *size_ptr,
            {
                bytes_from_uint256(*topic1_ptr),
            });
    }

    template <evmc_revision Rev>
    void log2(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t *const size_ptr, utils::uint256_t *const topic1_ptr,
        utils::uint256_t *const topic2_ptr)
    {
        log_impl<Rev>(
            exit_ctx,
            ctx,
            *offset_ptr,
            *size_ptr,
            {
                bytes_from_uint256(*topic1_ptr),
                bytes_from_uint256(*topic2_ptr),
            });
    }

    template <evmc_revision Rev>
    void log3(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t *const size_ptr, utils::uint256_t *const topic1_ptr,
        utils::uint256_t *const topic2_ptr, utils::uint256_t *const topic3_ptr)
    {
        log_impl<Rev>(
            exit_ctx,
            ctx,
            *offset_ptr,
            *size_ptr,
            {
                bytes_from_uint256(*topic1_ptr),
                bytes_from_uint256(*topic2_ptr),
                bytes_from_uint256(*topic3_ptr),
            });
    }

    template <evmc_revision Rev>
    void log4(
        ExitContext *exit_ctx, Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t *const size_ptr, utils::uint256_t *const topic1_ptr,
        utils::uint256_t *const topic2_ptr, utils::uint256_t *const topic3_ptr,
        utils::uint256_t *const topic4_ptr)
    {
        log_impl<Rev>(
            exit_ctx,
            ctx,
            *offset_ptr,
            *size_ptr,
            {
                bytes_from_uint256(*topic1_ptr),
                bytes_from_uint256(*topic2_ptr),
                bytes_from_uint256(*topic3_ptr),
                bytes_from_uint256(*topic4_ptr),
            });
    }
}
