#pragma once

#include <runtime/arithmetic.h>
#include <runtime/exit.h>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <limits>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void balance(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *address_ptr)
    {
        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->deduct_gas(2500);
            }
        }

        auto balance = ctx->host->get_balance(ctx->context, &address);
        *result_ptr = uint256_from_bytes32(balance);
    }

    template <evmc_revision Rev>
    void calldataload(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *i_ptr)
    {
        if (*i_ptr > std::numeric_limits<std::uint32_t>::max()) {
            *result_ptr = 0;
            return;
        }

        auto start = static_cast<std::uint32_t>(*i_ptr);

        if (ctx->env.input_data.size() <= start) {
            *result_ptr = 0;
            return;
        }

        auto len = std::min(
            saturating_sub(
                ctx->env.input_data.size(), static_cast<std::size_t>(start)),
            32ul);

        auto calldata = ctx->env.input_data.subspan(start, len);
        *result_ptr = uint256_from_span(calldata);
    }

    template <evmc_revision Rev>
    void copy_impl(
        Context *ctx, utils::uint256_t dest_offset_word,
        utils::uint256_t offset_word, utils::uint256_t size_word,
        std::span<std::uint8_t const> source)
    {
        MONAD_COMPILER_DEBUG_ASSERT(
            source.size() <= std::numeric_limits<std::uint32_t>::max());

        auto [dest_offset, size] =
            ctx->get_memory_offset_and_size(dest_offset_word, size_word);
        if (size == 0) {
            return;
        }

        auto size_in_words = (size + 31) / 32;
        ctx->deduct_gas(size_in_words * 3);

        ctx->expand_memory(saturating_add(dest_offset, size));

        auto start = [&] {
            if (offset_word > std::numeric_limits<std::uint32_t>::max()) {
                return static_cast<std::uint32_t>(source.size());
            }
            else {
                return std::min(
                    static_cast<std::uint32_t>(offset_word),
                    static_cast<std::uint32_t>(source.size()));
            }
        }();

        auto copy_size = std::min(
            size,
            saturating_sub(static_cast<std::uint32_t>(source.size()), start));

        if (copy_size > 0) {
            auto begin = source.begin() + start;
            std::copy(
                begin, begin + copy_size, ctx->memory.begin() + dest_offset);
        }

        auto size_diff = saturating_sub(size, copy_size);

        if (size_diff > 0) {
            auto begin =
                ctx->memory.begin() + saturating_add(dest_offset, copy_size);
            std::fill(begin, begin + size_diff, 0);
        }
    }

    template <evmc_revision Rev>
    void calldatacopy(
        Context *ctx, utils::uint256_t const *dest_offset_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        copy_impl<Rev>(
            ctx, *dest_offset_ptr, *offset_ptr, *size_ptr, ctx->env.input_data);
    }

    template <evmc_revision Rev>
    void codecopy(
        Context *ctx, utils::uint256_t const *dest_offset_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        copy_impl<Rev>(
            ctx, *dest_offset_ptr, *offset_ptr, *size_ptr, ctx->env.code);
    }

    template <evmc_revision Rev>
    void extcodecopy(
        Context *ctx, utils::uint256_t const *address_ptr,
        utils::uint256_t const *dest_offset_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        auto [dest_offset, size] =
            ctx->get_memory_offset_and_size(*dest_offset_ptr, *size_ptr);

        auto offset = clamp_cast<std::uint32_t>(*offset_ptr);

        if (size > 0) {
            ctx->expand_memory(size);

            auto size_in_words = (size + 31) / 32;
            ctx->deduct_gas(size_in_words * 3);
        }

        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->deduct_gas(2500);
            }
        }

        if (size > 0) {
            auto n = ctx->host->copy_code(
                ctx->context,
                &address,
                offset,
                ctx->memory.data() + dest_offset,
                size);

            auto begin = ctx->memory.begin() + dest_offset +
                         static_cast<std::uint32_t>(n);
            auto end = ctx->memory.begin() + dest_offset + size;

            std::fill(begin, end, 0);
        }
    }

    template <evmc_revision Rev>
    void returndatacopy(
        Context *ctx, utils::uint256_t const *dest_offset_ptr,
        utils::uint256_t const *offset_ptr, utils::uint256_t const *size_ptr)
    {
        auto [dest_offset, size] =
            ctx->get_memory_offset_and_size(*dest_offset_ptr, *size_ptr);

        auto offset = clamp_cast<std::uint32_t>(*offset_ptr);

        if (saturating_add(offset, size) > ctx->env.return_data.size()) {
            ctx->exit(StatusCode::InvalidMemoryAccess);
        }

        if (size > 0) {
            ctx->expand_memory(size);

            auto size_in_words = (size + 31) / 32;
            ctx->deduct_gas(size_in_words * 3);

            auto data = ctx->env.return_data.subspan(offset, size);
            std::copy(
                data.begin(), data.end(), ctx->memory.data() + dest_offset);
        }
    }

    template <evmc_revision Rev>
    void extcodehash(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *address_ptr)
    {
        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->deduct_gas(2500);
            }
        }

        auto hash = ctx->host->get_code_hash(ctx->context, &address);
        *result_ptr = uint256_from_bytes32(hash);
    }

    template <evmc_revision Rev>
    void extcodesize(
        Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *address_ptr)
    {
        auto address = address_from_uint256(*address_ptr);
        auto access_status = ctx->host->access_account(ctx->context, &address);

        if constexpr (Rev >= EVMC_BERLIN) {
            if (access_status == EVMC_ACCESS_COLD) {
                ctx->deduct_gas(2500);
            }
        }

        *result_ptr = ctx->host->get_code_size(ctx->context, &address);
    }
}
