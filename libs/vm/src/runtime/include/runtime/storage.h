#pragma once

#include <runtime/types.h>

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    template <evmc_revision Rev>
    void sload(
        void (*exit_fn)(Error), Context *ctx, utils::uint256_t *result,
        utils::uint256_t const *key)
    {
        (void)exit_fn;
        (void)ctx;
        (void)result;
        (void)key;
    }

    template <evmc_revision Rev>
    void sstore(
        void (*exit_fn)(Error), Context *ctx, utils::uint256_t const *key,
        utils::uint256_t const *value)
    {
        (void)exit_fn;
        (void)ctx;
        (void)key;
        (void)value;
    }
}
