#pragma once

#include <intx/intx.hpp>

#include <format>

using uint256_t = ::intx::uint256;

namespace monad::compiler
{
    using byte_offset = std::size_t;

    using block_id = std::size_t;

    inline constexpr block_id INVALID_BLOCK_ID =
        std::numeric_limits<block_id>::max();
}

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
