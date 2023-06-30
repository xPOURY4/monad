#pragma once

#include <monad/trie/config.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>

MONAD_TRIE_NAMESPACE_BEGIN

// default to 1 << 9 = 512 aligned value
template <std::unsigned_integral T, unsigned bits = 9>
inline constexpr T round_up_align(T const x) noexcept
{
    size_t constexpr mask = (1UL << bits) - 1;
    return (x + mask) & ~mask;
}

template <std::unsigned_integral T, unsigned bits = 9>
inline constexpr T round_down_align(T const x) noexcept
{
    size_t constexpr mask = ~((1UL << bits) - 1);
    return x & mask;
}

inline constexpr unsigned child_index(uint16_t const mask, unsigned const i)
{
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(mask & filter));
}

MONAD_TRIE_NAMESPACE_END