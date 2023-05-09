#include <monad/merkle/node.h>
#include <stdalign.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// helper struct: node of a upward pointing tree
typedef struct tnode_t tnode_t;
struct tnode_t
{
    tnode_t *parent;
    merkle_node_t *node;
    int8_t npending;
    uint8_t child_idx;
};

static_assert(sizeof(tnode_t) == 24);
static_assert(alignof(tnode_t) == 8);

static inline tnode_t *get_new_tnode(
    tnode_t *const parent_tnode, uint8_t const new_branch_arr_i,
    merkle_node_t *const new_branch)
{ // no npending
    tnode_t *const branch_tnode = (tnode_t *)cpool_ptr31(
        tmp_pool, cpool_reserve31(tmp_pool, sizeof(tnode_t)));
    cpool_advance31(tmp_pool, sizeof(tnode_t));

    branch_tnode->node = new_branch;
    branch_tnode->parent = parent_tnode;
    branch_tnode->child_idx = new_branch_arr_i;

    return branch_tnode;
}

#ifdef __cplusplus
}
#endif
