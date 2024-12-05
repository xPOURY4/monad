#pragma once

#include <cstdint>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    constexpr std::int64_t COST_ACCESS_COLD = 2100;
    constexpr std::int64_t COST_ACCESS_WARM = 100;
}
