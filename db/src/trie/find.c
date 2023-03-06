#include <monad/trie/find.h>
#include <monad/trie/nibble.h>

int find(
    trie_branch_node_t *root, unsigned char *path, unsigned char path_len,
    node_info node_stack[], int *stack_index)
{

    *stack_index = 0;
    int key_index = 0;

    unsigned char path_nibble;
    unsigned char node_nibble;

    // Initialize root
    trie_branch_node_t *node = root;
    bool persistent = false;

    while (key_index < path_len) {
        path_nibble = get_nibble(path, key_index);

        if (key_index >= node->path_len) {
            // Case 1: Reached the end of path in node

            // Push the node and nibble onto stack
            node_stack[*stack_index] = (node_info){
                .node = node, .nibble = path_nibble, .persistent = persistent};
            (*stack_index)++;

            // Need to check if there's an edge with path_nibble
            // to another node
            if (node->next[path_nibble]) {
                // Case 1.1: There's a subnode to traverse further

                // Update the node as child to keep traversing
                persistent = (node->fnext[path_nibble] != 0);
                node = (trie_branch_node_t *)node->next[path_nibble];
                key_index++;
                continue;
            }
            else {
                // Case 1.2: There's no edge
                return key_index;
            }
        }

        node_nibble = get_nibble(node->path, key_index);

        if (node_nibble != path_nibble) {
            // Case 2: Nibbles are not equal

            // Push the node and nibble onto stack
            node_stack[*stack_index] = (node_info){
                .node = node, .nibble = path_nibble, .persistent = persistent};
            (*stack_index)++;
            return key_index;
        }

        // node_nibble == path_nibble. keep traversing common prefix
        key_index++;
    }

    // Found the leaf node (assuming all keys are of equal length)
    // Push the leaf and nibble onto stack and return
    node_stack[*stack_index] = (node_info){
        .node = node, .nibble = path_nibble, .persistent = persistent};
    (*stack_index)++;

    return key_index;
}
