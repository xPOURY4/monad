#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/mpt/config.hpp>
#include <monad/mpt/nibbles_view.hpp>

#include <cassert>

MONAD_MPT_NAMESPACE_BEGIN

inline constexpr unsigned
compact_encode_len(unsigned const si, unsigned const ei)
{
    assert(ei >= si);
    return (ei - si) / 2 + 1;
}

/* See Appendix C. HexPrefix from Ethereum Yellow Paper
 * @param path is non-redundant
 * @param path_len is in nibbles
 * @param si: start nibble of path
 * @param ei: end nibble of path
 * @param terminating: if path[ei] is the end
 */
inline constexpr byte_string_view compact_encode(
    unsigned char *const res, NibblesView const relpath,
    bool const terminating) noexcept
{
    unsigned const ei = relpath.ei, si = relpath.si;
    unsigned char const *const path = relpath.data;
    assert(ei >= si);
    unsigned ci = si, path_len = ei - si;
    if (path_len == 0) {
        res[0] = 0x20;
        return byte_string_view(res, 1);
    }
    const bool odd = (path_len & 1u) != 0;
    res[0] = terminating ? 0x20 : 0x00;

    if (odd) {
        res[0] |= 0x10;
        res[0] |= get_nibble(path, ci);
        ++ci;
    }
    int res_ci = 2;
    while (ci != ei) {
        set_nibble(res, res_ci++, get_nibble(path, ci++));
    }
    return byte_string_view{res, path_len / 2 + 1};
}

MONAD_MPT_NAMESPACE_END