#include <monad/trie/node.h>
#include <stdlib.h>
#include <string.h>

// copy node
trie_branch_node_t *copy_node(trie_branch_node_t const *const node)
{
    if (!node) {
        return NULL;
    }
    size_t const size = node->type == BRANCH ? sizeof(trie_branch_node_t)
                                             : sizeof(trie_leaf_node_t);
    trie_branch_node_t *const new_node = malloc(size);
    memcpy(new_node, node, size);
    return new_node;
}

trie_leaf_node_t *get_new_leaf(
    unsigned char const *const path, unsigned char const path_len,
    trie_data_t const *const data)
{
    // create a new leaf node with the full path
    trie_leaf_node_t *const leaf =
        (trie_leaf_node_t *)malloc(sizeof(trie_leaf_node_t));
    leaf->type = LEAF;
    leaf->path_len = path_len;
    memcpy(leaf->path, path, (path_len + 1) / 2);
    copy_trie_data(&leaf->data, data);
    return leaf;
}

trie_branch_node_t *get_new_branch(
    unsigned char const *const new_path, unsigned char const new_path_len)
{
    // allocate memory for the branch
    trie_branch_node_t *const branch =
        (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    branch->type = BRANCH;

    // Set the path_len and copy the path
    branch->path_len = new_path_len;
    memcpy(branch->path, new_path, (new_path_len + 1) / 2);

    return branch;
}