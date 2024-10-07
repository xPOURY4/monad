#pragma once

#include <intx/intx.hpp>

#include <format>

namespace monad::compiler::uint256
{
    using uint256_t = ::intx::uint256;

    uint256_t signextend(uint256_t const& byte_index, uint256_t const& x);

    uint256_t byte(uint256_t const& byte_index, uint256_t const& x);

    uint256_t sar(uint256_t const& shift_index, uint256_t const &x);
}
