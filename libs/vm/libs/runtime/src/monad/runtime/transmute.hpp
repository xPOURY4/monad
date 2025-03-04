#pragma once

#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

namespace monad::runtime
{
    static_assert(sizeof(evmc_address) == 20);
    static_assert(sizeof(evmc_bytes32) == 32);
    static_assert(sizeof(utils::uint256_t) == 32);

    [[gnu::always_inline]]
    inline void uint256_store_be(uint8_t *dest, utils::uint256_t const &x)
    {
        std::uint64_t ts[4] = {
            std::byteswap(x[3]),
            std::byteswap(x[2]),
            std::byteswap(x[1]),
            std::byteswap(x[0])};
        std::memcpy(dest, ts, 32);
    }

    [[gnu::always_inline]]
    inline utils::uint256_t uint256_load_be(std::uint8_t const *bytes)
    {
        std::uint64_t ts[4];
        std::memcpy(&ts, bytes, 32);
        return {
            std::byteswap(ts[3]),
            std::byteswap(ts[2]),
            std::byteswap(ts[1]),
            std::byteswap(ts[0])};
    }

    inline utils::uint256_t
    uint256_load_bounded_be(std::uint8_t const *bytes, std::uint32_t max_len)
    {
        if (max_len >= 32) {
            return uint256_load_be(bytes);
        }
        std::uint64_t ts[4] = {};
        std::size_t offset = 0;
#pragma GCC unroll 5
        for (std::int32_t i = 4; i >= 0; --i) {
            std::uint32_t p = 1 << i;
            if (max_len & p) {
                std::memcpy(
                    reinterpret_cast<uint8_t *>(ts) + offset,
                    bytes + offset,
                    p);
                offset |= p;
            }
        }
        return {
            std::byteswap(ts[3]),
            std::byteswap(ts[2]),
            std::byteswap(ts[1]),
            std::byteswap(ts[0])};
    }

    [[gnu::always_inline]]
    inline utils::uint256_t
    uint256_load_bounded_le(std::uint8_t const *bytes, std::uint32_t max_len)
    {
        auto ret = utils::uint256_t{};
        std::memcpy(
            reinterpret_cast<std::uint8_t *>(&ret) + (32 - max_len),
            bytes,
            max_len);
        return intx::bswap(ret);
    }

    [[gnu::always_inline]]
    inline evmc::bytes32 bytes32_from_uint256(utils::uint256_t const &x)
    {
        alignas(std::uint64_t) evmc_bytes32 ret;
        uint256_store_be(ret.bytes, x);
        return ret;
    }

    [[gnu::always_inline]]
    inline evmc::address address_from_uint256(utils::uint256_t const &x)
    {
        auto bytes = intx::as_bytes(x);

        std::uint64_t t2;
        std::memcpy(&t2, bytes, 8);
        t2 = std::byteswap(t2);

        std::uint64_t t1;
        std::memcpy(&t1, bytes + 8, 8);
        t1 = std::byteswap(t1);

        std::uint32_t t0;
        std::memcpy(&t0, bytes + 16, 4);
        t0 = std::byteswap(t0);

        evmc_address ret;
        std::memcpy(ret.bytes, &t0, 4);
        std::memcpy(ret.bytes + 4, &t1, 8);
        std::memcpy(ret.bytes + 12, &t2, 8);
        return ret;
    }

    [[gnu::always_inline]]
    inline utils::uint256_t uint256_from_bytes32(evmc::bytes32 const &x)
    {
        return uint256_load_be(x.bytes);
    }

    [[gnu::always_inline]]
    inline utils::uint256_t uint256_from_address(evmc::address const &addr)
    {
        std::uint32_t t2;
        std::memcpy(&t2, addr.bytes, 4);
        t2 = std::byteswap(t2);

        std::uint64_t t1;
        std::memcpy(&t1, addr.bytes + 4, 8);
        t1 = std::byteswap(t1);

        std::uint64_t t0;
        std::memcpy(&t0, addr.bytes + 12, 8);
        t0 = std::byteswap(t0);

        alignas(utils::uint256_t) uint8_t ret[32];
        std::memcpy(ret, &t0, 8);
        std::memcpy(ret + 8, &t1, 8);
        std::memcpy(ret + 16, &t2, 4);
        std::memset(ret + 20, 0, 12);
        return std::bit_cast<utils::uint256_t>(ret);
    }

    template <std::uint8_t N>
        requires(N < 64)
    [[gnu::always_inline]]
    constexpr bool is_bounded_by_bits(utils::uint256_t const &x)
    {
        return ((x[0] >> N) | x[1] | x[2] | x[3]) == 0;
    }

    template <typename T>
    [[gnu::always_inline]]
    constexpr T clamp_cast(utils::uint256_t const &x) noexcept
    {
        return is_bounded_by_bits<std::numeric_limits<T>::digits>(x)
                   ? static_cast<T>(x)
                   : std::numeric_limits<T>::max();
    }
}
