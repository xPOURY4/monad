#pragma once

#include <monad/mpt/config.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <utility>

MONAD_MPT_NAMESPACE_BEGIN

struct Node
{
    uint16_t mask;
    uint16_t ref_count;
    unsigned char data[0];
};

inline constexpr uint16_t child_mask(Node const &node) noexcept
{
    return node.mask;
}

inline constexpr bool child_test(Node const &node, unsigned const i) noexcept
{
    return node.mask & (1u << i);
}

inline constexpr bool child_all(Node const &node) noexcept
{
    return node.mask == UINT16_MAX;
}

inline constexpr bool child_any(Node const &node) noexcept
{
    return node.mask;
}

inline constexpr bool child_none(Node const &node) noexcept
{
    return !node.mask;
}

inline constexpr unsigned child_count(Node const &node) noexcept
{
    return std::popcount(node.mask);
}

inline constexpr unsigned
child_index(Node const &node, unsigned const i) noexcept
{
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(node.mask & filter));
}

inline std::pair<unsigned char const *, unsigned char>
child_path(Node const &node, unsigned const i)
{
    unsigned const j = child_index(node, i);
    uint16_t const path_data = reinterpret_cast<uint16_t const *>(node.data)[j];
    uint16_t const path_off = path_data & ((1u << 10) - 1);
    unsigned char const path_len = path_data >> 10;
    unsigned char const *const path_str =
        node.data + sizeof(uint16_t) * child_count(node) + path_off;
    return {path_str, path_len};
}

MONAD_MPT_NAMESPACE_END
