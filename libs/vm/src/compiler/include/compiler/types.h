#pragma once

#include "uint256.h"

#include <format>

namespace monad::compiler
{
    using byte_offset = std::size_t;

    using block_id = std::size_t;

    using uint256_t = uint256::uint256_t;

    inline constexpr block_id INVALID_BLOCK_ID =
        std::numeric_limits<block_id>::max();
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
