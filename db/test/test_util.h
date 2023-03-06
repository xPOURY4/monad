#pragma once

#include <limits.h>
#include <monad/trie/node.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define STACK_SIZE 64 * 16

void do_commit(trie_branch_node_t *root);
void do_commit_recursive(trie_branch_node_t *node);
int count_num_leaves(trie_branch_node_t *root);

#ifdef __cplusplus
}
#endif