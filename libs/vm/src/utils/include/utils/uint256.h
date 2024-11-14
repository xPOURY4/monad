#pragma once

#include <intx/intx.hpp>

#include <format>

namespace monad::compiler
{
    using uint256_t = ::intx::uint256;

    uint256_t signextend(uint256_t const &byte_index, uint256_t const &x);
    uint256_t byte(uint256_t const &byte_index, uint256_t const &x);
    uint256_t sar(uint256_t const &shift_index, uint256_t const &x);
}

template <>
struct std::formatter<monad::compiler::uint256_t>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::compiler::uint256_t const &v, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "0x{}", intx::to_string(v, 16));
    }
};
