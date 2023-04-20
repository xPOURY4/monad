#include <monad/merkle/merge.h>
#include <monad/trie/nibble.h>

// presumption: prev_root and tmp_root are branch nodes
merkle_node_t *do_merge(
    merkle_node_t *const prev_root, trie_branch_node_t const *const tmp_root,
    unsigned char const pi)
{
    // construct new root, count number of children new root will have
    uint16_t const mask = prev_root->mask | tmp_root->subnode_bitmask;
    merkle_node_t *const new_root = get_new_merkle_node(mask);

    unsigned child_idx = 0;
    // Can do better with bit op loop
    for (int i = 0; i < 16; ++i) {
        if (prev_root->mask & 1u << i) {
            if (tmp_root->next[i]) { // both has branches
                merge_trie(
                    prev_root, i, tmp_root, i, pi + 1, new_root, child_idx);
            }
            else { // prev has branches, tmp not
                new_root->children[child_idx] =
                    prev_root->children[merkle_child_index(prev_root, i)];
            }
            ++child_idx;
        }
        else if (tmp_root->next[i]) { // prev no branch, tmp has
            set_merkle_child(new_root, child_idx, get_node(tmp_root->next[i]));
            ++child_idx;
        }
    }
    return new_root;
}

/* merge prev trie and tmp trie to generate a new version trie
   note that prev trie is immutable, always copy before modify
*/
void merge_trie(
    merkle_node_t *const prev_parent, uint8_t const prev_branch_i,
    trie_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_branch_arr_i)
{
    unsigned const prev_child_idx =
        merkle_child_index(prev_parent, prev_branch_i);
    unsigned char const prev_node_path_len =
        prev_parent->children[prev_child_idx].path_len;
    unsigned char *const prev_node_path =
        (unsigned char *const)prev_parent->children[prev_child_idx].path;

    trie_branch_node_t const *const tmp_node =
        get_node(tmp_parent->next[tmp_branch_i]);

    int next_nibble;
    unsigned char const min_path_len = prev_node_path_len > tmp_node->path_len
                                           ? tmp_node->path_len
                                           : prev_node_path_len;
    // -1: prev_is_shorter; 0: equal length, 1: tmp_is_shorter
    int tmp_is_shorter = prev_node_path_len > tmp_node->path_len;
    if (prev_node_path_len < tmp_node->path_len)
        tmp_is_shorter = -1;
    merkle_node_t *new_branch = NULL;
    unsigned char new_path_len;
    unsigned char *new_path;

    while (1) {
        if (min_path_len == pi) {
            if (tmp_is_shorter == 1) { // prev_node could be leaf
                next_nibble = get_nibble(prev_node_path, pi);
                if (tmp_node->next[next_nibble] != 0) {
                    // create a new branch same as tmp trie
                    new_branch = get_new_merkle_node(tmp_node->subnode_bitmask);

                    // copy each subtrie in tmp_node to new_branch_i
                    // in new trie except for next_nibble branch.
                    unsigned int child_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if (tmp_node->next[i]) {
                            if (i != next_nibble) {
                                set_merkle_child(
                                    new_branch,
                                    child_idx,
                                    get_node(tmp_node->next[i]));
                            }
                            ++child_idx;
                        }
                    }
                    // move one level down on the tmp trie under next_nibble
                    merge_trie(
                        prev_parent,
                        prev_branch_i,
                        tmp_node,
                        next_nibble,
                        pi + 1,
                        new_branch,
                        merkle_child_index(new_branch, next_nibble));
                }
                else { // no more next branch in tmp trie that matches the
                    // nibble in prev trie
                    // copy tmp_node to new_branch
                    new_branch = copy_tmp_trie(
                        tmp_node,
                        tmp_node->subnode_bitmask | 1u << next_nibble);

                    // add prev_node to new_branch's next_nibble branch
                    new_branch->children[merkle_child_index(
                        new_branch, next_nibble)] =
                        prev_parent->children[prev_child_idx];
                }
                new_path = (unsigned char *const)tmp_node->path;
                new_path_len = tmp_node->path_len;
            }
            else if (tmp_is_shorter == -1) { // prev path is shorter
                merkle_node_t *const prev_node =
                    get_merkle_next(prev_parent, prev_child_idx);
                // tmp may be a leaf
                next_nibble = get_nibble(tmp_node->path, pi);
                if (prev_node->mask & 1u << next_nibble) {
                    // same branch out at pi in new trie as in prev trie, except
                    // for next_nibble should be left open for next level merge

                    // create a new branch for the new trie
                    new_branch = copy_merkle_node(prev_node);
                    merge_trie(
                        prev_node,
                        next_nibble,
                        tmp_parent,
                        tmp_branch_i,
                        pi + 1,
                        new_branch,
                        merkle_child_index(new_branch, next_nibble));
                }
                else { // prev is shorter, no more matched next for prev trie
                    // branch out for both prev trie and tmp trie
                    // create a new branch for the new trie
                    uint16_t const mask = prev_node->mask | 1u << next_nibble;
                    new_branch = get_new_merkle_node(mask);

                    unsigned int child_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if ((new_branch->mask & 1u << i)) {
                            if (i != next_nibble) {
                                new_branch->children[child_idx] =
                                    prev_node->children[merkle_child_index(
                                        prev_node, i)];
                            }
                            else {
                                set_merkle_child(
                                    new_branch, child_idx, tmp_node);
                            }
                            ++child_idx;
                        }
                    }
                }
                new_path = prev_node_path;
                new_path_len = prev_node_path_len;
            }
            else {
                // if of the same length,
                // 1. leaves: assign data to new_parent
                // 2. branches: create a new branch node with branches
                // for each possible one = UNION(prev branches, tmp branches)
                if (tmp_node->type == LEAF) {
                    copy_trie_data(
                        &(new_parent->children[new_branch_arr_i].data),
                        &((trie_leaf_node_t *)tmp_node)->data);
                }
                else {
                    merkle_node_t *const prev_node =
                        get_merkle_next(prev_parent, prev_child_idx);
                    new_branch = do_merge(prev_node, tmp_node, pi);
                }
                new_path = prev_node_path;
                new_path_len = prev_node_path_len;
            }

            new_parent->children[new_branch_arr_i] = (merkle_child_info_t){
                .next = new_branch, .path_len = new_path_len};
            memcpy(
                new_parent->children[new_branch_arr_i].path,
                new_path,
                (1 + new_path_len) / 2);
            return;
        }
        // not reach the last nibble in current node yet
        unsigned char prev_nibble = get_nibble(prev_node_path, pi),
                      tmp_nibble = get_nibble(tmp_node->path, pi);
        if (prev_nibble == tmp_nibble) { // curr nibble matched
            ++pi;
            continue;
        }
        else {
            // curr nibble mismatch, create a new branch node with 2 children
            new_branch =
                get_new_merkle_node(1u << prev_nibble | 1u << tmp_nibble);

            new_parent->children[new_branch_arr_i] =
                (merkle_child_info_t){.next = new_branch, .path_len = pi};
            memcpy(
                new_parent->children[new_branch_arr_i].path,
                tmp_node->path,
                (1 + pi) / 2);

            // new_branch -> prev_nibble
            unsigned int prev_idx = prev_nibble > tmp_nibble;
            new_branch->children[prev_idx] =
                prev_parent->children[prev_child_idx];

            // new_branch -> tmp_nibble
            set_merkle_child(new_branch, !prev_idx, tmp_node);
            return;
        }
    }
}
