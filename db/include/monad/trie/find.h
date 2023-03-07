#pragma once

#include <monad/trie/node.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct node_info
{
    trie_branch_node_t *node;
    unsigned char nibble;
    bool persistent;
} node_info;

// Returns the number of nibbles traversed in the key
// i.e. the length of the largest common prefix of the key and the last node
// 		in the branch stack
int find(
    trie_branch_node_t *const root, unsigned char const *const path,
    unsigned char const path_len, node_info node_stack[], int *stack_index);

#ifdef __cplusplus
}
#endif
