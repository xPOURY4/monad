#pragma once

#include <monad/async/util.hpp>
#include <monad/core/assert.h>
#include <monad/mpt/config.hpp>

MONAD_MPT_NAMESPACE_BEGIN

using file_offset_t = MONAD_ASYNC_NAMESPACE::file_offset_t;

inline constexpr unsigned bitmask_index(uint16_t const mask, unsigned const i)
{
    MONAD_DEBUG_ASSERT(i < 16);
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(mask & filter));
}

inline constexpr unsigned bitmask_count(uint16_t const mask)
{
    return std::popcount(mask);
}

MONAD_MPT_NAMESPACE_END