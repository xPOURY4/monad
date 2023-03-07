#include "test_util.h"
#include <algorithm>
#include <numeric>
#include <vector>

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
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
                ++cnt;
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
    for (unsigned char i = 0; i < 16; ++i) {
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
            ++n_leaves;
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

int trie_metrics(trie_branch_node_t *root)
{
    // also record leaf node heights: min, max, 95% 75% 25% 5% avg
    int n_leaves = 0;
    if (root->type == UNKNOWN) {
        return n_leaves;
    }

    int key_len = 32;
    // use a stack for traversal
    trie_branch_node_t *node;
    trie_branch_node_t *stack[STACK_SIZE];
    int height_stack[STACK_SIZE];
    int curr_h;
    int64_t si = 0;
    stack[si] = root;
    height_stack[si] = 0; // root height is 0

    int max_h = 0, min_h = INT_MAX, sum_h = 0;
    // int min_subnodes = INT_MAX, max_subnodes = 0, sum_subnodes = 0;

    // min max avg subnodes for each level
    std::vector<int32_t> min_subnodes(key_len * 2, INT_MAX),
        max_subnodes(key_len * 2, 0), sum_subnodes(key_len * 2, 0),
        n_branches(key_len * 2, 0), sum_path_len(key_len * 2, 0),
        min_path_len(key_len * 2, INT_MAX), max_path_len(key_len * 2, 0);

    while (si >= 0) {
        // pop the current node on stack
        curr_h = height_stack[si];
        node = stack[si--];

        if (node->type == LEAF) {
            ++n_leaves;
            max_h = MAX(max_h, curr_h);
            min_h = MIN(min_h, curr_h);
            sum_h += curr_h;
            continue;
        }
        assert(node->type == BRANCH);
        // if it's branch node
        if (node->nsubnodes > 16) {
            printf(
                "exception with the branch\n info: curr_h %d, nsubnodes %d\n",
                curr_h,
                node->nsubnodes);
        }
        min_subnodes[curr_h] = MIN(min_subnodes[curr_h], node->nsubnodes);
        max_subnodes[curr_h] = MAX(max_subnodes[curr_h], node->nsubnodes);
        min_path_len[curr_h] = MIN(min_path_len[curr_h], node->path_len);
        max_path_len[curr_h] = MAX(max_path_len[curr_h], node->path_len);
        sum_subnodes[curr_h] += node->nsubnodes;
        sum_path_len[curr_h] += node->path_len;
        ++n_branches[curr_h];

        for (int i = 0; i < 16; ++i) {
            if (node->next[i]) {
                stack[++si] = (trie_branch_node_t *)node->next[i];
                height_stack[si] = curr_h + 1;
            }
        }
    }
    // stats summary
    int h;
    int32_t tot_subnodes = 0, tot_n_branches = 0, tot_path_len = 0;
    for (h = 0; h < key_len * 2; ++h) {
        if (n_branches[h] == 0) {
            break;
        }
        fprintf(
            stdout,
            "\tLevel %d, n_branch %d, min_subnodes %d, max_subnodes %d, "
            "avg_subnodes %.4f, min_path_len %d, max_path_len %d, avg_path_len "
            "%.4f\n",
            h,
            n_branches[h],
            min_subnodes[h],
            max_subnodes[h],
            (double)sum_subnodes[h] / n_branches[h],
            min_path_len[h],
            max_path_len[h],
            (double)sum_path_len[h] / n_branches[h]);
        tot_subnodes += sum_subnodes[h];
        tot_n_branches += n_branches[h];
        tot_path_len += sum_path_len[h];
    }
    assert(h == max_h);

    fprintf(
        stdout,
        "\tOverall: min_h %d, max_h %d, avg_h %.4f\n",
        min_h,
        max_h,
        (double)sum_h / n_leaves);

    fprintf(
        stdout,
        "\t\t n_branch %d, avg_subnodes %.4f, avg_path_len %.4f"
        "\n\t\t > n_leaf / n_branch %.4f\n",
        tot_n_branches,
        (double)tot_subnodes / tot_n_branches,
        (double)tot_path_len / tot_n_branches,
        (double)n_leaves / tot_n_branches);

    fflush(stdout);
    return n_leaves;
}