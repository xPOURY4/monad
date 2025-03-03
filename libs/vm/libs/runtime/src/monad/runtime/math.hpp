#pragma once

#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

extern "C" void monad_runtime_mul(
    ::intx::uint256 *, ::intx::uint256 const *, ::intx::uint256 const *);

extern "C" void monad_runtime_mul_192(
    ::intx::uint<192> *, ::intx::uint<192> const *, ::intx::uint<192> const *);

extern "C" void monad_runtime_mul_128(
    ::intx::uint<128> *, ::intx::uint<128> const *, ::intx::uint<128> const *);

namespace monad::runtime
{
    template <evmc_revision Rev>
    constexpr void (*mul)(
        utils::uint256_t *, utils::uint256_t const *,
        utils::uint256_t const *) = monad_runtime_mul;

    template <evmc_revision Rev>
    void udiv(
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
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
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
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
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
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
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
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
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr, utils::uint256_t const *n_ptr)
    {
        if (*n_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::addmod(*a_ptr, *b_ptr, *n_ptr);
    }

    template <evmc_revision Rev>
    void mulmod(
        utils::uint256_t *result_ptr, utils::uint256_t const *a_ptr,
        utils::uint256_t const *b_ptr, utils::uint256_t const *n_ptr)
    {
        if (*n_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::mulmod(*a_ptr, *b_ptr, *n_ptr);
    }

    template <evmc_revision Rev>
    void
    exp(Context *ctx, utils::uint256_t *result_ptr,
        utils::uint256_t const *a_ptr, utils::uint256_t const *exponent_ptr)
    {
        auto exponent_byte_size = intx::count_significant_bytes(*exponent_ptr);

        auto exponent_cost = [] -> decltype(exponent_byte_size) {
            if constexpr (Rev >= EVMC_SPURIOUS_DRAGON) {
                return 50;
            }
            else {
                return 10;
            }
        }();

        ctx->deduct_gas(exponent_byte_size * exponent_cost);

        *result_ptr = intx::exp(*a_ptr, *exponent_ptr);
    }
}
