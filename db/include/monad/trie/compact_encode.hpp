#pragma once

#include <cstddef>
#include <monad/core/byte_string.hpp>
#include <monad/trie/config.hpp>
#include <monad/trie/nibble.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

/* See Appendix C. HexPrefix from Ethereum Yellow Paper
 * @param path is non-redundant
 * @param path_len is in nibbles
 * @param si: start nibble of path
 * @param ei: end nibble of path
 * @param terminating: if path[ei] is the end
 */
[[nodiscard]] constexpr byte_string_view compact_encode(
    unsigned char const *path, uint8_t si, uint8_t ei, bool terminating)
{
    unsigned ci = si, path_len = ei - si;
    unsigned char *res = (unsigned char *)malloc(path_len / 2 + 1);
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

MONAD_TRIE_NAMESPACE_END