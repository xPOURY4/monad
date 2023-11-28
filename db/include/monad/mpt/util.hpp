#pragma once

#include <monad/async/util.hpp>
#include <monad/core/assert.h>
#include <monad/core/hex_literal.hpp>
#include <monad/mpt/config.hpp>

#include <concepts>

MONAD_MPT_NAMESPACE_BEGIN

using chunk_offset_t = MONAD_ASYNC_NAMESPACE::chunk_offset_t;
using chunk_offset_t_hasher = MONAD_ASYNC_NAMESPACE::chunk_offset_t_hasher;
using file_offset_t = MONAD_ASYNC_NAMESPACE::file_offset_t;

using MONAD_ASYNC_NAMESPACE::CPU_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::CPU_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::DISK_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::DISK_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::DMA_PAGE_BITS;
using MONAD_ASYNC_NAMESPACE::DMA_PAGE_SIZE;
using MONAD_ASYNC_NAMESPACE::INVALID_OFFSET;

using MONAD_ASYNC_NAMESPACE::round_down_align;
using MONAD_ASYNC_NAMESPACE::round_up_align;

static constexpr uint8_t INVALID_BRANCH = 255;
static constexpr uint8_t INVALID_PATH_INDEX = 255;
static constexpr unsigned CACHE_LEVEL = 5;

static byte_string const empty_trie_hash = [] {
    using namespace ::monad::literals;
    return 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex;
}();

inline constexpr unsigned
bitmask_index(uint16_t const mask, unsigned const i) noexcept
{
    MONAD_DEBUG_ASSERT(i < 16);
    uint16_t const filter = UINT16_MAX >> (16 - i);
    return static_cast<unsigned>(
        std::popcount(static_cast<uint16_t>(mask & filter)));
}

//! convert an integral's least significant N bytes to a size-N byte string
template <int N, std::unsigned_integral V>
inline byte_string serialise_as_big_endian(V n)
{
    MONAD_ASSERT(N <= sizeof(V));

    // std::byteswap is C++23 only, using GCC intrinsic instead
    if constexpr (std::endian::native != std::endian::big) {
        if constexpr (sizeof(V) <= 2) {
            n = __builtin_bswap16(n);
        }
        else if constexpr (sizeof(V) == 4) {
            n = __builtin_bswap32(n);
        }
        else if constexpr (sizeof(V) == 8) {
            n = __builtin_bswap64(n);
        }
        else {
            return serialise_as_big_endian<N>(static_cast<uint64_t>(n));
        }
    }
    auto arr = std::bit_cast<std::array<unsigned char, sizeof(V)>>(n);
    return byte_string{arr.data() + sizeof(n) - N, N};
}

MONAD_MPT_NAMESPACE_END
