#pragma once

#include <monad/rlp/config.hpp>

#include <monad/core/byte_string.hpp>

#include <bit>
#include <cstddef>
#include <cstring>

MONAD_RLP_NAMESPACE_BEGIN

namespace impl
{
    constexpr size_t length_length(size_t const n)
    {
        /*
        size_t const len_bits = std::bit_width(n);
        size_t const len_bytes = (len_bits + 7) / 8;
        return len_bytes;
        */
        size_t const lz_bits = std::countl_zero(n);
        size_t const lz_bytes = lz_bits / 8;
        return sizeof(size_t) - lz_bytes;
    }

    /**
     * always stores sizeof(size_t) bytes
     */
    constexpr unsigned char *encode_length(unsigned char *d, size_t n)
    {
        size_t const lz_bits = std::countl_zero(n);
        size_t const lz_bytes = lz_bits / 8;
        n <<= lz_bytes;
        size_t const n_be = [&n] {
            if constexpr (std::endian::native == std::endian::little) {
                return std::byteswap(n);
            }
            return n;
        }();
        *((size_t *)d) = n_be;
        return d + (sizeof(size_t) - lz_bytes);
    }
}

constexpr size_t string_length(byte_string_view const s)
{
    if (s.size() == 1 and s[0] <= 0x7F) {
        return 1;
    }
    else if (s.size() <= 55) {
        return 1 + s.size();
    }
    else {
        return 1 + impl::length_length(s.size()) + s.size();
    }
}

constexpr unsigned char *
encode_string(unsigned char *d, byte_string_view const s)
{
    if (s.size() == 1 and s[0] <= 0x7F) {
        *d++ = s[0];
    }
    else if (s.size() <= 55) {
        *d++ = 0x80 + s.size();
        std::memcpy(d, s.data(), s.size());
        d += s.size();
    }
    else {
        *d++ = 0xB7 + impl::length_length(s.size());
        d = impl::encode_length(d, s.size());
        std::memcpy(d, s.data(), s.size());
        d += s.size();
    }
    return d;
}

MONAD_RLP_NAMESPACE_END
