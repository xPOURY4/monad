#pragma once

#include <intx/intx.hpp>

#include <format>
#include <limits>

namespace monad::vm::utils
{
    using uint256_t = ::intx::uint256;

    uint256_t signextend(uint256_t const &byte_index, uint256_t const &x);
    uint256_t byte(uint256_t const &byte_index, uint256_t const &x);
    uint256_t sar(uint256_t const &shift_index, uint256_t const &x);
    uint256_t countr_zero(uint256_t const &x);

    constexpr size_t popcount(uint256_t const &x)
    {
        return static_cast<size_t>(
            std::popcount(x[0]) + std::popcount(x[1]) + std::popcount(x[2]) +
            std::popcount(x[3]));
    }

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

    inline size_t countl_zero(uint256_t const &x)
    {
        size_t cnt = 0;
        for (size_t i = 0; i < 4; i++) {
            cnt += static_cast<size_t>(std::countl_zero(x[3 - i]));
            if (cnt != ((i + 1U) * 64U)) {
                return cnt;
            }
        }
        return cnt;
    }

    inline size_t bit_width(uint256_t const &x)
    {
        return static_cast<size_t>(std::numeric_limits<uint256_t>::digits) -
               countl_zero(x);
    }

    consteval uint256_t pow2(size_t n)
    {
        return uint256_t{1} << n;
    }
}

template <>
struct std::formatter<monad::vm::utils::uint256_t>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::vm::utils::uint256_t const &v, std::format_context &ctx) const
    {
        return std::format_to(ctx.out(), "0x{}", intx::to_string(v, 16));
    }
};
