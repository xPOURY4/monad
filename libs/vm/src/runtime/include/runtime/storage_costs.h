#pragma once

#include <cstdint>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    struct StoreCost
    {
        std::int64_t gas_cost;
        std::int64_t gas_refund;
    };

    template <evmc_revision Rev>
    struct StorageCostTable
    {
        static_assert(Rev >= EVMC_FRONTIER && Rev <= EVMC_CANCUN);
        static constexpr StoreCost costs[9] = {};
    };

    template <evmc_revision Rev>
    constexpr StoreCost store_cost(evmc_storage_status status)
    {
        return StorageCostTable<Rev>::costs[status];
    }

    template <>
    struct StorageCostTable<EVMC_FRONTIER>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_HOMESTEAD>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_TANGERINE_WHISTLE>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_SPURIOUS_DRAGON>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_BYZANTIUM>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_CONSTANTINOPLE>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 200, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 200, .gas_refund = -15000},
            StoreCost{.gas_cost = 200, .gas_refund = 15000},
            StoreCost{.gas_cost = 200, .gas_refund = -10200},
            StoreCost{.gas_cost = 200, .gas_refund = 19800},
            StoreCost{.gas_cost = 200, .gas_refund = 4800},
        };
    };

    template <>
    struct StorageCostTable<EVMC_PETERSBURG>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
        };
    };

    template <>
    struct StorageCostTable<EVMC_ISTANBUL>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 800, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 5000, .gas_refund = 15000},
            StoreCost{.gas_cost = 5000, .gas_refund = 0},
            StoreCost{.gas_cost = 800, .gas_refund = -15000},
            StoreCost{.gas_cost = 800, .gas_refund = 15000},
            StoreCost{.gas_cost = 800, .gas_refund = -10800},
            StoreCost{.gas_cost = 800, .gas_refund = 19200},
            StoreCost{.gas_cost = 800, .gas_refund = 4200},
        };
    };

    template <>
    struct StorageCostTable<EVMC_BERLIN>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 15000},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -15000},
            StoreCost{.gas_cost = 100, .gas_refund = 15000},
            StoreCost{.gas_cost = 100, .gas_refund = -12200},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        };
    };

    template <>
    struct StorageCostTable<EVMC_LONDON>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        };
    };

    template <>
    struct StorageCostTable<EVMC_PARIS>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        };
    };

    template <>
    struct StorageCostTable<EVMC_SHANGHAI>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        };
    };

    template <>
    struct StorageCostTable<EVMC_CANCUN>
    {
        static constexpr StoreCost costs[9] = {
            StoreCost{.gas_cost = 100, .gas_refund = 0},
            StoreCost{.gas_cost = 20000, .gas_refund = 0},
            StoreCost{.gas_cost = 2900, .gas_refund = 4800},
            StoreCost{.gas_cost = 2900, .gas_refund = 0},
            StoreCost{.gas_cost = 100, .gas_refund = -4800},
            StoreCost{.gas_cost = 100, .gas_refund = 4800},
            StoreCost{.gas_cost = 100, .gas_refund = -2000},
            StoreCost{.gas_cost = 100, .gas_refund = 19900},
            StoreCost{.gas_cost = 100, .gas_refund = 2800},
        };
    };
}
