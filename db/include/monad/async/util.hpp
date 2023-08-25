#pragma once

#include "config.hpp"

#include <concepts>
#include <type_traits>

MONAD_ASYNC_NAMESPACE_BEGIN

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

//! Creates already deleted file so no need to clean it up
//! after
extern int make_temporary_inode() noexcept;

MONAD_ASYNC_NAMESPACE_END