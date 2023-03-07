#include <monad/trie/nibble.h>
#include <monad/trie/update.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Note:
    - we represent persistent subnodes using non-zero fnext value
    - two types of nodes: branch and leaf, where branch node has a mixed
   functionality as branch and extension node in MPT.
*/

/* Helper functions */

/*
Update parent node_stack[parent_si]'s child to new_node, persistent specifies if
it's on disk. If we ever meet a persistent parent, we create a new parent and
updates its ancestors on the stack iteratively until a mutable parent
is found
*/
static inline void update_ancestors(
    trie_branch_node_t *new_node, bool persistent, node_info node_stack[],
    int8_t parent_si)
{
    trie_branch_node_t *parent, *new_parent;
    unsigned char parent_edge;

    for (int8_t i = parent_si; i >= 0; i--) {
        parent = node_stack[i].node;
        parent_edge = node_stack[i].nibble;

        if (node_stack[i].persistent) {
            // create a copy of the persistent parent
            new_parent = copy_node(parent);

            // set the child node to new_node
            new_parent->next[parent_edge] = (unsigned char *)new_node;
            new_parent->fnext[parent_edge] = persistent ? ULONG_MAX : 0;
            new_node = new_parent;
            persistent = false; // new_node is a copy, thus mutable
        }
        else { // parent is a mutable copy. set the child node to new_node
            parent->next[parent_edge] = (unsigned char *)new_node;
            parent->fnext[parent_edge] = persistent ? ULONG_MAX : 0;
            return;
        }
    }
}

/*
   Erase a node from node_stack[parent_si] with branch idx `to_erase`, if only
   one child left after the erase, merge child to parent. Update ancesters
   iteratively whenever new node is created.
*/
static inline void
erase_node_merge_parent(node_info node_stack[], int8_t parent_si)
{
    if (parent_si < 0) {
        return;
    }

    trie_branch_node_t *parent = node_stack[parent_si].node;
    unsigned char to_erase = node_stack[parent_si].nibble;

    trie_branch_node_t *new_parent;

    if (parent->nsubnodes - 1 <= 1 && parent->path_len != 0) {
        unsigned char only_child =
            ffs(parent->subnode_bitmask & ~(0x01 << to_erase)) - 1;
        bool persistent = parent->fnext[only_child];

        // make the only child the new_parent
        new_parent = (trie_branch_node_t *)parent->next[only_child];
        update_ancestors(new_parent, persistent, node_stack, parent_si - 1);

        // free the replaced mutable parent
        if (!node_stack[parent_si].persistent) {
            free(parent);
        }
    }
    else { // more than one child left
        if (node_stack[parent_si].persistent) {
            // need to copy the persistent parent before modify it
            new_parent = copy_node(parent);
            update_ancestors(new_parent, false, node_stack, parent_si - 1);
            parent = new_parent;
        }
        parent->next[to_erase] = NULL;
        parent->fnext[to_erase] = 0;
        parent->subnode_bitmask &= ~(0x01 << to_erase); // clear that bit
        parent->nsubnodes--;
    }
}

/* helper functions end */

void upsert(
    trie_branch_node_t *root, unsigned char *path, uint8_t path_len,
    trie_data_t *data)
{
    node_info node_stack[path_len + 1];
    int stack_index = 0;

    int parsed = find(root, path, path_len, node_stack, &stack_index);
    node_info last_node = node_stack[stack_index - 1];

    if (parsed == path_len) {
        // parsed the entire key. last_node is the leaf
        if (cmp_trie_data(&(last_node.node)->data, data)) {
            // if persistent, create a copy and update ancestors
            if (last_node.persistent) {
                last_node.node = copy_node(last_node.node);
                update_ancestors(
                    last_node.node, false, node_stack, stack_index - 2);
            }
            copy_trie_data(&(last_node.node)->data, data);
        }
    }
    else if (parsed >= (last_node.node)->path_len) {
        // reached the end of path in a branch. last_node is the parent
        // branch whose child should be the new leaf
        trie_branch_node_t *branch = last_node.node;
        // make a copy of the branch if it is persistent
        if (last_node.persistent) {
            branch = copy_node(branch);
            update_ancestors(branch, false, node_stack, stack_index - 2);
        }

        // create a new leaf for the new key and update branch
        branch->next[last_node.nibble] =
            (unsigned char *)get_new_leaf(path, path_len, data);
        branch->fnext[last_node.nibble] = 0;
        branch->subnode_bitmask |= 0x01 << (last_node.nibble);
        branch->nsubnodes++;
    }
    else {
        // found an unequal nibble when parsing the path
        trie_branch_node_t *new_branch = get_new_branch(path, parsed);

        // push the node with unequal nibble down
        unsigned char unequal_nibble =
            get_nibble((last_node.node)->path, parsed);
        new_branch->next[unequal_nibble] = (unsigned char *)last_node.node;
        // get the fnext value from its parent
        trie_branch_node_t *parent = node_stack[stack_index - 2].node;
        unsigned char parent_edge = node_stack[stack_index - 2].nibble;
        new_branch->fnext[unequal_nibble] = parent->fnext[parent_edge];
        new_branch->subnode_bitmask |= 0x01 << unequal_nibble;
        new_branch->nsubnodes++;

        // create a leaf for the new key
        new_branch->next[last_node.nibble] =
            (unsigned char *)get_new_leaf(path, path_len, data);
        new_branch->subnode_bitmask |= 0x01 << last_node.nibble;
        new_branch->nsubnodes++;

        // update parents iteratively with the new_branch
        update_ancestors(new_branch, false, node_stack, stack_index - 2);
    }
}

void erase(
    trie_branch_node_t *root, unsigned char *path, unsigned char path_len)
{
    // record the stack from root to curr
    // whenever a node is removed and its parent only got 1 subnode left, we
    // squash it to a leaf node do this iteratively until a parent got >1
    // subnodes left.

    node_info node_stack[path_len + 1];
    int stack_index = 0;
    int parsed = find(root, path, path_len, node_stack, &stack_index);

    // if parsed length is less than key length, then the key is not found
    if (parsed != path_len) {
        return;
    }

    // found key to delete
    node_info last_node = node_stack[stack_index - 1];
    assert(root != last_node.node);

    erase_node_merge_parent(node_stack, stack_index - 2);

    // if the leaf was a copy already, free it
    if (!last_node.persistent) {
        free(last_node.node);
    }
}
