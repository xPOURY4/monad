#pragma once

#include <monad/trie/data.h>
#include <monad/trie/find.h>
#include <monad/trie/node.h>

#ifdef __cplusplus
extern "C"
{
#endif

void upsert(
    trie_branch_node_t *root, unsigned char *path, unsigned char path_len,
    trie_data_t *);

void erase(
    trie_branch_node_t *root, unsigned char *path, unsigned char path_len);

#ifdef __cplusplus
}
#endif
