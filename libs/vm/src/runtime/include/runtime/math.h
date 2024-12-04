#pragma once

#include <runtime/exit.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void udiv(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr)
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = *a_ptr / *b_ptr;
    }

    template <evmc_revision Rev>
    void sdiv(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr)
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::sdivrem(*a_ptr, *b_ptr).quot;
    }

    template <evmc_revision Rev>
    void umod(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr)
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = *a_ptr % *b_ptr;
    }

    template <evmc_revision Rev>
    void smod(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr)
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::sdivrem(*a_ptr, *b_ptr).rem;
    }

    template <evmc_revision Rev>
    void addmod(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr, utils::uint256_t const *n_ptr)
    {
        if (*n_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::addmod(*a_ptr, *b_ptr, *n_ptr);
    }

    template <evmc_revision Rev>
    void
    exp(Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t *const a_ptr, utils::uint256_t *const exponent_ptr)
    {
        auto exponent_byte_size = intx::count_significant_bytes(*exponent_ptr);

        auto exponent_cost = [] {
            if constexpr (Rev >= EVMC_SPURIOUS_DRAGON) {
                return 50;
            }
            else {
                return 10;
            }
        }();

        auto gas_cost = exponent_byte_size * exponent_cost;

        ctx->gas_remaining -= gas_cost;
        if (MONAD_COMPILER_UNLIKELY(ctx->gas_remaining < 0)) {
            ctx->exit(StatusCode::OutOfGas);
        }

        *result_ptr = intx::exp(*a_ptr, *exponent_ptr);
    }

    template <evmc_revision Rev>
    void signextend(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t *const b_ptr,
        utils::uint256_t *const x_ptr)
    {
        *result_ptr = utils::signextend(*b_ptr, *x_ptr);
    }

    template <evmc_revision Rev>
    void byte(
        Context *, utils::uint256_t *result_ptr, utils::uint256_t *const i_ptr,
        utils::uint256_t *const x_ptr)
    {
        *result_ptr = utils::byte(*i_ptr, *x_ptr);
    }

    template <evmc_revision Rev>
    void
    shl(Context *, utils::uint256_t *result_ptr, utils::uint256_t *const i_ptr,
        utils::uint256_t *const x_ptr)
    {
        *result_ptr = (*x_ptr) << (*i_ptr);
    }

    template <evmc_revision Rev>
    void
    shr(Context *, utils::uint256_t *result_ptr, utils::uint256_t *const i_ptr,
        utils::uint256_t *const x_ptr)
    {
        *result_ptr = (*x_ptr) >> (*i_ptr);
    }

    template <evmc_revision Rev>
    void
    sar(Context *, utils::uint256_t *result_ptr, utils::uint256_t *const i_ptr,
        utils::uint256_t *const x_ptr)
    {
        *result_ptr = utils::sar(*i_ptr, *x_ptr);
    }
}
