#pragma once

static inline unsigned char
get_nibble(unsigned char const *const d, unsigned const n)
{
    unsigned char r = d[n / 2];
    if (n % 2 == 0) {
        r >>= 4;
    }
    else {
        r &= 0xF;
    }
    return r;
}

static inline void
set_nibble(unsigned char *const d, unsigned const n, unsigned char const v)
{
    unsigned char r = d[n / 2];
    if (n % 2 == 0) {
        r &= 0xF;
        r |= (v << 4);
    }
    else {
        r &= 0xF0;
        r |= (v & 0xF);
    }
    d[n / 2] = r;
}

/**
 * nibble strings stored as [len] ++ data
 */

static inline unsigned char nibble_strlen(unsigned char const *const s)
{
    return s[0];
}

static inline unsigned char *nibble_strcpy(
    unsigned char *const restrict dest, unsigned char const *const restrict src)
{
    unsigned char const len = nibble_strlen(src);
    unsigned const n = (len + 1) / 2;
    __builtin_memcpy(dest, src, n + 1);
    return dest;
}

static inline unsigned char *nibble_strcat(
    unsigned char *const restrict dest, unsigned char const *const restrict src)
{
    unsigned char const dest_len = nibble_strlen(dest);
    unsigned char const src_len = nibble_strlen(src);
    if (dest_len % 2 == 0) {
        unsigned const dest_n = (dest_len + 1) / 2;
        unsigned const src_n = (src_len + 1) / 2;
        __builtin_memcpy(dest + dest_n + 1, src + 1, src_n);
    }
    else {
        // TODO optimize memcpy with shift
        for (unsigned char i = 0; i < src_len; ++i) {
            set_nibble(dest + 1, dest_len + i, get_nibble(src + 1, i));
        }
    }
    dest[0] += src[0];
    return dest;
}
