#pragma once

#include <monad/vm/core/assert.h>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/runtime/uint256.hpp>

#include <evmc/evmc.hpp>

// It is assumed that if the `result` pointer overlaps with `left` and/or
// `right`, then `result` pointer is equal to `left` and/or `right`.
extern "C" void monad_vm_runtime_mul_192(
    monad::vm::utils::uint256_t *result,
    monad::vm::utils::uint256_t const *left,
    monad::vm::utils::uint256_t const *right) noexcept;

namespace monad::vm::runtime
{
    constexpr void (*mul)(
        vm::utils::uint256_t *, vm::utils::uint256_t const *,
        vm::utils::uint256_t const *) noexcept = monad_vm_runtime_mul;

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

        *result_ptr = vm::utils::sdivrem(*a_ptr, *b_ptr).quot;
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

        *result_ptr = vm::utils::sdivrem(*a_ptr, *b_ptr).rem;
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

        *result_ptr = vm::utils::addmod(*a_ptr, *b_ptr, *n_ptr);
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

        *result_ptr = vm::utils::mulmod(*a_ptr, *b_ptr, *n_ptr);
    }

    template <evmc_revision Rev>
    constexpr void
    exp(Context *ctx, vm::utils::uint256_t *result_ptr,
        vm::utils::uint256_t const *a_ptr,
        vm::utils::uint256_t const *exponent_ptr) noexcept
    {
        auto exponent_byte_size =
            vm::utils::count_significant_bytes(*exponent_ptr);

        auto exponent_cost = [] -> decltype(exponent_byte_size) {
            if constexpr (Rev >= EVMC_SPURIOUS_DRAGON) {
                return 50;
            }
            else {
                return 10;
            }
        }();

        ctx->deduct_gas(exponent_byte_size * exponent_cost);

        *result_ptr = vm::utils::exp(*a_ptr, *exponent_ptr);
    }
}
