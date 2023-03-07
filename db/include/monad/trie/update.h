#pragma once

#include <monad/trie/data.h>
#include <monad/trie/find.h>
#include <monad/trie/node.h>

#ifdef __cplusplus
extern "C"
{
#endif

void upsert(
    trie_branch_node_t *const root, unsigned char const *const path,
    const uint8_t path_len, trie_data_t const *const);

void erase(
    trie_branch_node_t *const root, unsigned char const *const path,
    unsigned char const path_len);

#ifdef __cplusplus
}
#endif
