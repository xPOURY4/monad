#pragma once

#include <runtime/transmute.h>
#include <runtime/types.h>

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <array>

namespace monad::runtime
{
    struct StoreCost
    {
        std::int64_t gas_cost;
        std::int64_t gas_refund;
    };

    constexpr std::array<std::array<StoreCost, 9>, 14> sstore_costs = {{
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 200, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 200, .gas_refund = -15000},
            StoreCost{.gas_cost = 200, .gas_refund = 15000},
            StoreCost{.gas_cost = 200, .gas_refund = -10200},
            StoreCost{.gas_cost = 200, .gas_refund = 19800},
            StoreCost{.gas_cost = 200, .gas_refund = 4800},
        },
        {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        },
        {
            StoreCost{.gas_cost = 800, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 800, .gas_refund = -15000},
            StoreCost{.gas_cost = 800, .gas_refund = 15000},
            StoreCost{.gas_cost = 800, .gas_refund = -10800},
            StoreCost{.gas_cost = 800, .gas_refund = 19200},
            StoreCost{.gas_cost = 800, .gas_refund = 4200},
        },
        {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 15000},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -15000},
            StoreCost{.gas_cost = 100, .gas_refund = 15000},
            StoreCost{.gas_cost = 100, .gas_refund = -12200},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        },
        {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        },
        {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        },
        {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        },
        {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        },
        {
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
            StoreCost{.gas_cost = 0, .gas_refund = 0},
        },
    }};

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
