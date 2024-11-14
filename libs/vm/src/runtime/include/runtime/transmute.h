#pragma once

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    evmc::bytes32 from_uint256(utils::uint256_t);
    utils::uint256_t from_bytes32(evmc::bytes32);
}
