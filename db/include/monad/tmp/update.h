#pragma once

#include <monad/tmp/node.h>
#include <monad/trie/data.h>

#ifdef __cplusplus
extern "C"
{
#endif

// upsert to a mutable in-mem trie
void upsert(
    uint32_t const root, unsigned char const *const path,
    const uint8_t path_len, trie_data_t const *const);

// void erase(uint32_t const root, unsigned char const *const path,
//     unsigned char const path_len);

#ifdef __cplusplus
}
#endif
