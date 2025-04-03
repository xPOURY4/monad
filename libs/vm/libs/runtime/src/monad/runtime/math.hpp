#pragma once

#include <monad/runtime/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

extern "C" void monad_runtime_mul(
    ::intx::uint256 *, ::intx::uint256 const *,
    ::intx::uint256 const *) noexcept;

extern "C" void monad_runtime_mul_192(
    ::intx::uint256 *, ::intx::uint256 const *,
    ::intx::uint256 const *) noexcept;

namespace monad::runtime
{
    constexpr void (*mul)(
        vm::utils::uint256_t *, vm::utils::uint256_t const *,
        vm::utils::uint256_t const *) noexcept = monad_runtime_mul;

    constexpr void udiv(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr) noexcept
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = *a_ptr / *b_ptr;
    }

    constexpr void sdiv(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr) noexcept
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::sdivrem(*a_ptr, *b_ptr).quot;
    }

    constexpr void umod(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr) noexcept
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = *a_ptr % *b_ptr;
    }

    constexpr void smod(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr) noexcept
    {
        if (*b_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::sdivrem(*a_ptr, *b_ptr).rem;
    }

    constexpr void addmod(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr,
        vm::utils::uint256_t const *n_ptr) noexcept
    {
        if (*n_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::addmod(*a_ptr, *b_ptr, *n_ptr);
    }

    constexpr void mulmod(
        vm::utils::uint256_t *result_ptr, vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *b_ptr,
        vm::utils::uint256_t const *n_ptr) noexcept
    {
        if (*n_ptr == 0) {
            *result_ptr = 0;
            return;
        }

        *result_ptr = intx::mulmod(*a_ptr, *b_ptr, *n_ptr);
    }

    template <evmc_revision Rev>
    constexpr void
    exp(Context *ctx, vm::utils::uint256_t *result_ptr,
        vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *exponent_ptr) noexcept
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

    /**
     * GCC doesn't unroll the default intx implementation of 256-bit add with
     * carry, so we need to replace it with our own implementation to ensure
     * that we get good codegen.
     */
    [[gnu::always_inline]] inline vm::utils::uint256_t
    unrolled_add(vm::utils::uint256_t const &a, vm::utils::uint256_t const &b)
    {
        auto [s0, c0] = intx::addc(a[0], b[0], false);
        auto [s1, c1] = intx::addc(a[1], b[1], c0);
        auto [s2, c2] = intx::addc(a[2], b[2], c1);
        auto [s3, c3] = intx::addc(a[3], b[3], c2);
        return {s0, s1, s2, s3};
    }
}
