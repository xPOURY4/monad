#pragma once

#include <limits.h>
#include <monad/merkle/node.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

uint64_t precommit_add(merkle_node_t *node, int *cnt, int *n_compute);
uint64_t precommit_add_last(merkle_node_t *node);

#ifdef __cplusplus
}
#endif