#pragma once

#include <monad/utils/uint256.hpp>

#include <format>

namespace monad::compiler
{
    using byte_offset = std::size_t;

    using block_id = std::size_t;

    using uint256_t = utils::uint256_t;

    inline constexpr block_id INVALID_BLOCK_ID =
        std::numeric_limits<block_id>::max();

    template <class... Ts>
    struct Cases : Ts...
    {
        using Ts::operator()...;
    };
}
