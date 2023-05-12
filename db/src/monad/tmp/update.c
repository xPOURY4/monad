#include <monad/tmp/node.h>
#include <monad/tmp/update.h>
#include <monad/trie/nibble.h>
#include <stdbool.h>

// implement an in-memory upsert, all nodes are mutable
void upsert(
    uint32_t const root, unsigned char const *const path,
    const uint8_t path_len, trie_data_t const *const data, bool erase)
{
    int key_index = 0;
    unsigned char path_nibble;
    unsigned char node_nibble;

    uint32_t node_i = root;
    unsigned char nibble = 0;
    trie_branch_node_t *node = get_node(node_i), *parent_node = NULL;

    while (key_index < path_len) {
        path_nibble = get_nibble(path, key_index);

        if (key_index >= node->path_len) {
            // Case 1: Reached the end of path in node
            // check if there's an edge with path_nibble
            // to another node
            if (node->subnode_bitmask & 1u << path_nibble) {
                // Case 1.1: There's a subnode to traverse further
                // Update the node as child to keep traversing
                parent_node = node;
                nibble = path_nibble;
                node_i = parent_node->next[path_nibble];
                node = get_node(node_i);

                continue;
                key_index++;
            }
            else {
                // Case 1.2: There's no edge
                // add a new branch at current node
                node->next[path_nibble] =
                    get_new_leaf(path, path_len, data, erase);
                ++node->nsubnodes;
                node->subnode_bitmask |= 1u << path_nibble;
                return;
            }
        }

        node_nibble = get_nibble(node->path, key_index);
        if (node_nibble != path_nibble) {
            // Case 2: Nibbles are not equal
            // create a new branch that branches out at node_i, update parent
            uint32_t new_branch_i = get_new_branch(path, key_index);
            parent_node->next[nibble] = new_branch_i;
            trie_branch_node_t *new_branch = get_node(new_branch_i);
            new_branch->next[path_nibble] =
                get_new_leaf(path, path_len, data, erase);
            new_branch->next[node_nibble] = node_i;
            new_branch->nsubnodes = 2;
            new_branch->subnode_bitmask |=
                (1u << path_nibble) | (1u << node_nibble);
            return;
        }
        // node_nibble == path_nibble. keep traversing common prefix
        key_index++;
    }
    return;
}
