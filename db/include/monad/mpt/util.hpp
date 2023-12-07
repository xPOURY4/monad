#pragma once

#include <monad/async/util.hpp>
#include <monad/core/assert.h>
#include <monad/core/hex_literal.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>

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
template <int N, std::unsigned_integral UnsignedInteger>
inline byte_string serialize_as_big_endian(UnsignedInteger n)
{
    MONAD_ASSERT(N <= sizeof(UnsignedInteger));

    // std::byteswap is C++23 only, using GCC intrinsic instead
    if constexpr (std::endian::native != std::endian::big) {
        if constexpr (sizeof(UnsignedInteger) <= 2) {
            n = __builtin_bswap16(n);
        }
        else if constexpr (sizeof(UnsignedInteger) == 4) {
            n = __builtin_bswap32(n);
        }
        else if constexpr (sizeof(UnsignedInteger) == 8) {
            n = __builtin_bswap64(n);
        }
        else {
            return serialize_as_big_endian<N>(static_cast<uint64_t>(n));
        }
    }
    auto arr =
        std::bit_cast<std::array<unsigned char, sizeof(UnsignedInteger)>>(n);
    return byte_string{arr.data() + sizeof(n) - N, N};
}

template <std::unsigned_integral UnsignedInteger>
inline UnsignedInteger deserialize_from_big_endian(NibblesView const in)
{
    if (in.nibble_size() > sizeof(UnsignedInteger) * 2) {
        throw std::runtime_error(
            "input bytes to deserialize must be less than or "
            "equal to sizeof output type\n");
    }
    UnsignedInteger out = 0;
    UnsignedInteger bit =
        static_cast<UnsignedInteger>(1UL << ((in.nibble_size() - 1) * 4));
    for (auto i = 0; i < in.nibble_size(); ++i, bit >>= 4) {
        out += static_cast<UnsignedInteger>(
            in.get(static_cast<unsigned char>(i)) * bit);
    }
    return out;
}

MONAD_MPT_NAMESPACE_END
