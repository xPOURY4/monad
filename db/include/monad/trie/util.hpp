#pragma once

#include "config.hpp"

#include <monad/async/util.hpp>

#include <bit> // for popcount
#include <cstdint>

MONAD_TRIE_NAMESPACE_BEGIN

using file_offset_t = MONAD_ASYNC_NAMESPACE::file_offset_t;
using use_anonymous_inode_tag = MONAD_ASYNC_NAMESPACE::use_anonymous_inode_tag;

using MONAD_ASYNC_NAMESPACE::CPU_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::CPU_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::DISK_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::DISK_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::DMA_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::DMA_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::INVALID_OFFSET;

using MONAD_ASYNC_NAMESPACE::round_down_align;
using MONAD_ASYNC_NAMESPACE::round_up_align;

//! The maximum disk storage used by a serialised merkle node
static constexpr uint16_t MAX_DISK_NODE_SIZE = 2690;

//! The storage root offset size
static constexpr uint8_t ROOT_OFFSET_SIZE = 8;

inline constexpr unsigned child_index(uint16_t const mask, unsigned const i)
{
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return std::popcount(static_cast<uint16_t>(mask & filter));
}

MONAD_TRIE_NAMESPACE_END
