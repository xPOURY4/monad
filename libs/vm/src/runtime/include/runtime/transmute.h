#pragma once

#include <utils/uint256.h>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    evmc::bytes32 bytes_from_uint256(utils::uint256_t);
    evmc::address address_from_uint256(utils::uint256_t);
    utils::uint256_t uint256_from_bytes32(evmc::bytes32);
    utils::uint256_t uint256_from_address(evmc::address);
}
