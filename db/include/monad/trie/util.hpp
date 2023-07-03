#pragma once

#include <monad/trie/config.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>

MONAD_TRIE_NAMESPACE_BEGIN

#define PAGE_SIZE 4096

template <class T>
constexpr T round_up_4k(T const x)
{
    return ((x - 1) >> 12 << 12) + PAGE_SIZE;
}

template <class T>
constexpr T round_down_4k(T const x)
{
    return x >> 12 << 12;
}

static constexpr unsigned child_index(uint16_t const mask, unsigned const i)
{
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(mask & filter));
}

MONAD_TRIE_NAMESPACE_END