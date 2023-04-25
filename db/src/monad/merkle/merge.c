#include <monad/merkle/merge.h>
#include <monad/trie/nibble.h>

// presumption: prev_root and tmp_root are branch nodes
merkle_node_t *do_merge(
    merkle_node_t *const prev_root, trie_branch_node_t const *const tmp_root,
    unsigned char const pi, tnode_t *curr)
{
    // construct new root, count number of children new root will have
    uint16_t const mask = prev_root->mask | tmp_root->subnode_bitmask;
    merkle_node_t *const new_root = get_new_merkle_node(mask);
    // construct current list node, and connect to parent in list
    curr->node = new_root;
    curr->npending = new_root->nsubnodes;

    unsigned child_idx = 0;
    // Can do better with bit op loop
    for (int i = 0; i < 16; ++i) {
        if (prev_root->mask & 1u << i) {
            if (tmp_root->next[i]) { // both has branches
                merge_trie(
                    prev_root,
                    merkle_child_index(prev_root, i),
                    tmp_root,
                    i,
                    pi + 1,
                    new_root,
                    child_idx,
                    curr);
                assert(curr->node == new_root);
            }
            else { // prev has branches, tmp not
                new_root->children[child_idx] =
                    prev_root->children[merkle_child_index(prev_root, i)];
                --curr->npending;
            }
            ++child_idx;
        }
        else if (tmp_root->next[i]) { // prev no branch, tmp has
            set_merkle_child_from_tmp(
                new_root, child_idx, get_node(tmp_root->next[i]));
            --curr->npending;
            ++child_idx;
        }
    }
    return new_root;
}

/* - merge prev trie and tmp trie to generate a new version trie
     note that prev trie is immutable, always copy before modify
   - pipelined precommit immediately after a subtrie is done with updates.
     this is through an upward pointing tree (child points to parent).
     calculate parent's data[i] when curr node has no children pending.
   - call upward_update_data() from poll() after async read.
*/
void merge_trie(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    trie_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_branch_arr_i, tnode_t *parent_tnode)
{
    unsigned char const prev_node_path_len =
        prev_parent->children[prev_child_i].path_len;
    unsigned char *const prev_node_path =
        (unsigned char *const)prev_parent->children[prev_child_i].path;

    trie_branch_node_t const *const tmp_node =
        get_node(tmp_parent->next[tmp_branch_i]);

    int next_nibble;
    unsigned char const min_path_len = prev_node_path_len > tmp_node->path_len
                                           ? tmp_node->path_len
                                           : prev_node_path_len;
    // -1: prev_is_shorter; 0: equal length, 1: tmp_is_shorter
    int const tmp_is_shorter = (tmp_node->path_len < prev_node_path_len) -
                               (prev_node_path_len < tmp_node->path_len);
    merkle_node_t *new_branch = NULL;
    unsigned char new_path_len;
    unsigned char *new_path;
    merkle_node_t *prev_node = prev_parent->children[prev_child_i].next;

    while (1) {
        if (min_path_len == pi) {
            tnode_t *branch_tnode = NULL;
            if (tmp_is_shorter == 1) { // prev_node could be leaf
                next_nibble = get_nibble(prev_node_path, pi);
                if (tmp_node->next[next_nibble] != 0) {
                    // create a new branch same as tmp trie
                    new_branch = get_new_merkle_node(tmp_node->subnode_bitmask);
                    branch_tnode = get_new_tnode(
                        parent_tnode, new_branch_arr_i, new_branch);

                    // copy each subtrie in tmp_node to new_branch_i
                    // in new trie except for next_nibble branch.
                    unsigned int child_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if (tmp_node->next[i]) {
                            if (i != next_nibble) {
                                set_merkle_child_from_tmp(
                                    new_branch,
                                    child_idx,
                                    get_node(tmp_node->next[i]));
                            }
                            ++child_idx;
                        }
                    }
                    branch_tnode->npending = 1;
                    // move one level down on the tmp trie under next_nibble
                    merge_trie(
                        prev_parent,
                        prev_child_i,
                        tmp_node,
                        next_nibble,
                        pi + 1,
                        new_branch,
                        merkle_child_index(new_branch, next_nibble),
                        branch_tnode);
                    assert(branch_tnode->node == new_branch);
                }
                else { // no more next branch in tmp trie that matches the
                    // nibble in prev trie
                    // copy tmp_node to new_branch
                    new_branch = get_new_merkle_node(
                        tmp_node->subnode_bitmask | 1u << next_nibble);
                    unsigned child_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if (new_branch->mask & 1u << i) {
                            if (tmp_node->next[i]) {
                                set_merkle_child_from_tmp(
                                    new_branch,
                                    child_idx,
                                    get_node(tmp_node->next[i]));
                            }
                            else {
                                new_branch->children[child_idx] =
                                    prev_parent->children[prev_child_i];
                            }
                            ++child_idx;
                        }
                    }
                }
                new_path = (unsigned char *const)tmp_node->path;
                new_path_len = tmp_node->path_len;
            }
            else if (tmp_is_shorter == -1) { // prev path is shorter
                if (!prev_node) {
                    merge_uring_data_t *uring_data = get_merge_uring_data(
                        prev_parent,
                        prev_child_i,
                        tmp_parent,
                        tmp_branch_i,
                        pi,
                        new_parent,
                        new_branch_arr_i,
                        parent_tnode);
                    async_read_request(uring_data);
                    return; // async callback will pickup when finished
                }
                // tmp may be a leaf
                next_nibble = get_nibble(tmp_node->path, pi);
                if (prev_node->mask & 1u << next_nibble) {
                    // same branch out at pi in new trie as in prev trie, except
                    // for next_nibble should be left open for next level merge

                    // create a new branch for the new trie
                    new_branch = copy_merkle_node(prev_node);
                    branch_tnode = get_new_tnode(
                        parent_tnode, new_branch_arr_i, new_branch);
                    branch_tnode->npending = 1;
                    merge_trie(
                        prev_node,
                        merkle_child_index(prev_node, next_nibble),
                        tmp_parent,
                        tmp_branch_i,
                        pi + 1,
                        new_branch,
                        merkle_child_index(new_branch, next_nibble),
                        branch_tnode);
                    assert(branch_tnode->node == new_branch);
                }
                else { // prev is shorter, no more matched next for prev trie
                    // branch out for both prev trie and tmp trie
                    // create a new branch for the new trie
                    new_branch = get_new_merkle_node(
                        prev_node->mask | 1u << next_nibble);
                    unsigned int child_idx = 0;
                    for (int i = 0; i < 16; ++i) {
                        if ((new_branch->mask & 1u << i)) {
                            if (i != next_nibble) {
                                new_branch->children[child_idx] =
                                    prev_node->children[merkle_child_index(
                                        prev_node, i)];
                            }
                            else {
                                set_merkle_child_from_tmp(
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
                    --parent_tnode->npending; // new_branch is NULL
                }
                else {
                    if (!prev_node) {
                        merge_uring_data_t *uring_data = get_merge_uring_data(
                            prev_parent,
                            prev_child_i,
                            tmp_parent,
                            tmp_branch_i,
                            pi,
                            new_parent,
                            new_branch_arr_i,
                            parent_tnode);
                        async_read_request(uring_data);
                        return; // async callback will pickup when finished
                    }
                    // do_merge update branch_tnode's node and npending
                    branch_tnode = get_new_tnode(
                        parent_tnode, new_branch_arr_i, new_branch);
                    new_branch =
                        do_merge(prev_node, tmp_node, pi, branch_tnode);
                    assert(branch_tnode->node == new_branch);
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

            if (new_branch) {
                if (!branch_tnode || !branch_tnode->npending) {
                    new_parent->children[new_branch_arr_i].data.words[0] =
                        sum_data_first_word(new_branch);
                    --parent_tnode->npending;
                }
            }
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
            // new_branch -> prev_nibble
            unsigned int prev_idx = prev_nibble > tmp_nibble;
            new_branch->children[prev_idx] =
                prev_parent->children[prev_child_i];
            // new_branch -> tmp_nibble
            set_merkle_child_from_tmp(new_branch, !prev_idx, tmp_node);

            // update new_parent's specific child
            new_parent->children[new_branch_arr_i] =
                (merkle_child_info_t){.next = new_branch, .path_len = pi};
            memcpy(
                new_parent->children[new_branch_arr_i].path,
                tmp_node->path,
                (1 + pi) / 2);
            new_parent->children[new_branch_arr_i].data.words[0] =
                sum_data_first_word(new_branch);
            --parent_tnode->npending;

            return;
        }
    }
}

void upward_update_data(tnode_t *curr_tnode)
{
    if (!curr_tnode) {
        return;
    }
    while (!curr_tnode->npending && curr_tnode->parent) {
        // ready to sum for curr->node and update data in parent
        merkle_node_t *parent = curr_tnode->parent->node;
        parent->children[curr_tnode->child_idx].data.words[0] =
            sum_data_first_word(curr_tnode->node);
        --curr_tnode->parent->npending;
        curr_tnode = curr_tnode->parent;
    }
}
