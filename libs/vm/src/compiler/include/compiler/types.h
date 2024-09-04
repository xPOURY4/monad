#pragma once

#include <intx/intx.hpp>

#include <format>

using uint256_t = ::intx::uint256;

using byte_offset = std::size_t;

template <>
struct std::formatter<uint256_t>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(uint256_t const &v, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "0x{}", intx::to_string(v, 16));
    }
};
