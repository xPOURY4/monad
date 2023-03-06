#include "test_util.h"

/* Here implements the commit helper function */
void do_commit(trie_branch_node_t *root)
{
    // root: the up-to-date in-memory root node
    // commit all new updates
    // for each level nodes
    // we set fnext[i] = ULONG_MAX for all i where next[i]!= NULL
    if (root->type == UNKNOWN) {
        return;
    }
    int cnt = 0;
    // use a stack for traversal
    trie_branch_node_t *node;
    trie_branch_node_t *stack[STACK_SIZE];
    int64_t si = 0;
    stack[si] = root;
    while (si >= 0) {
        // pop the current node on stack
        node = stack[si--];
        if (node->type == LEAF) {
            continue;
        }
        // set all fnext of curr node
        // push all non-NULL next onto stack
        for (int i = 0; i < 16; ++i) {
            if (node->next[i] && node->fnext[i] == 0) {
                cnt++;
                node->fnext[i] = ULONG_MAX;
                stack[++si] = (trie_branch_node_t *)node->next[i];
            }
        }
    }
    printf("%d nodes were converted to persistent nodes during commit\n", cnt);
}

void do_commit_recursive(trie_branch_node_t *node)
{
    if (node->type == LEAF) {
        return;
    }
    for (unsigned char i = 0; i < 16; i++) {
        if (node->next[i] && node->fnext[i] == 0) {
            node->fnext[i] = ULONG_MAX;
            do_commit_recursive((trie_branch_node_t *)node->next[i]);
        }
    }
}

int count_num_leaves(trie_branch_node_t *root)
{
    int n_leaves = 0;
    if (root->type == UNKNOWN) {
        return n_leaves;
    }

    // use a stack for traversal
    trie_branch_node_t *node;
    trie_branch_node_t *stack[STACK_SIZE];
    int64_t si = 0;
    stack[si] = root;
    while (si >= 0) {
        // pop the current node on stack
        node = stack[si--];
        if (node->type == LEAF) {
            n_leaves++;
            continue;
        }
        for (int i = 0; i < 16; ++i) {
            if (node->next[i]) {
                stack[++si] = (trie_branch_node_t *)node->next[i];
            }
        }
    }
    return n_leaves;
}
