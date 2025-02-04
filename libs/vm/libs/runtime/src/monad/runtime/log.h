#pragma once

#include <monad/runtime/transmute.h>
#include <monad/runtime/types.h>
#include <monad/utils/uint256.h>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void log_impl(
        Context *ctx, utils::uint256_t const &offset_word,
        utils::uint256_t const &size_word,
        std::span<evmc::bytes32 const> topics)
    {
        if (ctx->env.evmc_flags == EVMC_STATIC) {
            ctx->exit(StatusCode::StaticModeViolation);
        }

        Memory::Offset offset;
        auto size = ctx->get_memory_offset(size_word);

        if (*size > 0) {
            offset = ctx->get_memory_offset(offset_word);
            ctx->expand_memory(offset + size);
            ctx->deduct_gas(size * bin<8>);
        }

        ctx->host->emit_log(
            ctx->context,
            &ctx->env.recipient,
            ctx->memory.data + *offset,
            *size,
            topics.data(),
            topics.size());
    }

    template <evmc_revision Rev>
    void log0(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *size_ptr)
    {
        log_impl<Rev>(ctx, *offset_ptr, *size_ptr, {});
    }

    template <evmc_revision Rev>
    void log1(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *size_ptr, utils::uint256_t const *topic1_ptr)
    {
        log_impl<Rev>(
            ctx,
            *offset_ptr,
            *size_ptr,
            {{
                bytes32_from_uint256(*topic1_ptr),
            }});
    }

    template <evmc_revision Rev>
    void log2(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *size_ptr, utils::uint256_t const *topic1_ptr,
        utils::uint256_t const *topic2_ptr)
    {
        log_impl<Rev>(
            ctx,
            *offset_ptr,
            *size_ptr,
            {{
                bytes32_from_uint256(*topic1_ptr),
                bytes32_from_uint256(*topic2_ptr),
            }});
    }

    template <evmc_revision Rev>
    void log3(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *size_ptr, utils::uint256_t const *topic1_ptr,
        utils::uint256_t const *topic2_ptr, utils::uint256_t const *topic3_ptr)
    {
        log_impl<Rev>(
            ctx,
            *offset_ptr,
            *size_ptr,
            {{
                bytes32_from_uint256(*topic1_ptr),
                bytes32_from_uint256(*topic2_ptr),
                bytes32_from_uint256(*topic3_ptr),
            }});
    }

    template <evmc_revision Rev>
    void log4(
        Context *ctx, utils::uint256_t const *offset_ptr,
        utils::uint256_t const *size_ptr, utils::uint256_t const *topic1_ptr,
        utils::uint256_t const *topic2_ptr, utils::uint256_t const *topic3_ptr,
        utils::uint256_t const *topic4_ptr)
    {
        log_impl<Rev>(
            ctx,
            *offset_ptr,
            *size_ptr,
            {{
                bytes32_from_uint256(*topic1_ptr),
                bytes32_from_uint256(*topic2_ptr),
                bytes32_from_uint256(*topic3_ptr),
                bytes32_from_uint256(*topic4_ptr),
            }});
    }
}
