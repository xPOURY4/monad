#include "test_util.h"
#include <ethash/keccak.h>
#include <monad/mem/cpool.h>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

// add up first word
uint64_t
precommit_add(merkle_node_t *const node, int *const cnt, int *const num_compute)
{
    // recompute updated node data
    uint64_t sum_data = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].next && !node->children[i].fnext) {
            node->children[i].data.words[0] =
                precommit_add(node->children[i].next, cnt, num_compute);
        }
        // for untouched siblings or leaf siblings, just add it up
        sum_data += node->children[i].data.words[0];
        *cnt += 1;
    }
    *num_compute += 1;
    return sum_data;
}

// add up last word
uint64_t precommit_add_last(merkle_node_t *const node)
{
    uint64_t sum_data = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].next && !node->children[i].fnext) {
            node->children[i].data.words[3] =
                precommit_add_last(node->children[i].next);
        }
        sum_data += node->children[i].data.words[3];
    }
    return sum_data;
}