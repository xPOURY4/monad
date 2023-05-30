#pragma once
#include <monad/trie/config.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

#define PAGE_SIZE 4096

template <typename T>
constexpr T high_4k_aligned(T data)
{
    return ((data - 1) >> 12 << 12) + PAGE_SIZE;
}

template <typename T>
constexpr T low_4k_aligned(T data)
{
    return data >> 12 << 12;
}

MONAD_TRIE_NAMESPACE_END