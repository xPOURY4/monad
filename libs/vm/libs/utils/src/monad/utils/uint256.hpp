#pragma once

#include <intx/intx.hpp>

#include <format>

namespace monad::utils
{
    using uint256_t = ::intx::uint256;

    uint256_t signextend(uint256_t const &byte_index, uint256_t const &x);
    uint256_t byte(uint256_t const &byte_index, uint256_t const &x);
    uint256_t sar(uint256_t const &shift_index, uint256_t const &x);

    /**
     * Parse a range of raw bytes with length `n` into a 256-bit big-endian word
     * value.
     *
     * If there are fewer than `n` bytes remaining in the source data (that is,
     * `remaining < n`), then treat the input as if it had been padded to the
     * right with zero bytes.
     */
    uint256_t
    from_bytes(std::size_t n, std::size_t remaining, uint8_t const *src);

    /**
     * Parse a range of raw bytes with length `n` into a 256-bit big-endian word
     * value.
     *
     * There must be at least `n` bytes readable from `src`; if there are not,
     * use the safe overload that allows for the number of bytes remaining to be
     * specified.
     */
    uint256_t from_bytes(std::size_t n, uint8_t const *src);
}

template <>
struct std::formatter<monad::utils::uint256_t>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::utils::uint256_t const &v, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "0x{}", intx::to_string(v, 16));
    }
};
