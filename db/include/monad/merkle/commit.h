#pragma once

#include <monad/merkle/node.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct write_trie_retdata_t
{
    int64_t fnext;
    uint64_t first_word_data;
} write_trie_retdata_t;

void do_commit(int fd, merkle_node_t *root);

write_trie_retdata_t write_trie(
    int fd, unsigned char **buffer, size_t *buffer_idx, merkle_node_t *node,
    int64_t *block_off);

void free_trie(merkle_node_t *);

#ifdef __cplusplus
}
#endif
