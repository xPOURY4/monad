#pragma once

#include <monad/trie/config.hpp>

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <limits.h>
#include <linux/types.h> // for __u64
#include <sys/user.h> // for PAGE_SIZE

MONAD_TRIE_NAMESPACE_BEGIN

#if PAGE_SIZE != 4096
    #error                                                                     \
        "Non 4Kb CPU PAGE_SIZE detected. Refactoring the codebase to support that would be wise."
#endif

//! The same type io_uring uses for offsets into files during i/o
using file_offset_t = __u64;

//! The maximum disk storage used by a serialised merkle node
static constexpr uint16_t MAX_DISK_NODE_SIZE = 2674;
//! The invalid file offset
static constexpr file_offset_t INVALID_OFFSET = file_offset_t(-1);

//! The CPU page size and bits to assume
static constexpr uint16_t CPU_PAGE_SIZE = 4096;
static constexpr uint16_t CPU_PAGE_BITS = 12;

//! The storage i/o page size and bits to assume
static constexpr uint16_t DISK_PAGE_SIZE = 512;
static constexpr uint16_t DISK_PAGE_BITS = 9;

//! The types suitable for rounding up or down
template <class T, unsigned bits>
concept safely_roundable_type =
    std::unsigned_integral<T> && (__CHAR_BIT__ * sizeof(T)) > bits;

template <unsigned bits, safely_roundable_type<bits> T>
inline constexpr T round_up_align(T const x) noexcept
{
    T constexpr mask = (T(1) << bits) - 1;
    return (x + mask) & ~mask;
}

template <unsigned bits, safely_roundable_type<bits> T>
inline constexpr T round_down_align(T const x) noexcept
{
    T constexpr mask = ~((T(1) << bits) - 1);
    return x & mask;
}

inline constexpr unsigned child_index(uint16_t const mask, unsigned const i)
{
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(mask & filter));
}

MONAD_TRIE_NAMESPACE_END