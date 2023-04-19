#pragma once

#include <monad/merkle/node.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

void do_commit(int fd, merkle_node_t *root);

int write_buffer_to_disk(int fd, unsigned char *buffer);

int64_t write_trie(
    int fd, unsigned char **buffer, size_t *buffer_idx, merkle_node_t *node,
    int64_t *block_off);

#ifdef __cplusplus
}
#endif
