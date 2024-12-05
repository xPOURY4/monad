#pragma once

#include <cstdint>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    constexpr std::int64_t COST_ACCESS_COLD = 2100;
    constexpr std::int64_t COST_ACCESS_WARM = 100;

    consteval std::int64_t call_base_gas(evmc_revision rev)
    {
        if (rev < EVMC_TANGERINE_WHISTLE) {
            return 40;
        }

        if (rev < EVMC_BERLIN) {
            return 700;
        }

        return COST_ACCESS_WARM;
    }

    consteval std::int64_t extcodehash_base_gas(evmc_revision rev)
    {
        if (rev < EVMC_ISTANBUL) {
            return 400;
        }

        if (rev < EVMC_BERLIN) {
            return 700;
        }

        return COST_ACCESS_WARM;
    }
}
