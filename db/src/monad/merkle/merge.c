#include <monad/merkle/merge.h>
#include <monad/trie/io.h>
#include <monad/trie/nibble.h>

// presumption: prev_root and tmp_root are branch nodes
merkle_node_t *do_merge(
    merkle_node_t *const prev_root, trie_branch_node_t const *const tmp_root,
    unsigned char const pi, tnode_t *curr_tnode)
{
    merkle_node_t *new_root = get_new_merkle_node(
        prev_root->valid_mask | tmp_root->subnode_bitmask, prev_root->path_len);
    // construct current tnode, and connect to parent tnode
    curr_tnode->node = new_root;
    curr_tnode->npending = new_root->nsubnodes;

    // Can do better with bit op loop
    for (int i = 0, child_idx = 0; i < 16; ++i) {
        if (prev_root->valid_mask & 1u << i) {
            if (tmp_root->next[i]) { // both have branches
                // new_root may be recreated during merge_trie
                merge_trie(
                    prev_root,
                    merkle_child_index(prev_root, i),
                    tmp_root,
                    i,
                    pi + 1,
                    new_root,
                    i,
                    curr_tnode);
            }
            else { // prev has branches, tmp do not
                merkle_child_info_t *prev_child =
                    &prev_root->children[merkle_child_index(prev_root, i)];
                new_root->children[child_idx] = *prev_child;
                prev_child->next = NULL;
                prev_child->data = NULL;
                --curr_tnode->npending;
            }
            ++child_idx;
        }
        else if (tmp_root->next[i]) { // prev no branch, tmp has
            // this case must be account creation
            set_merkle_child_from_tmp(
                new_root, child_idx++, get_node(tmp_root->next[i]));
            --curr_tnode->npending;
        }
    }
    return new_root;
}

/*   - merge prev trie and tmp trie to generate a new version trie
 *     note that prev trie is immutable, always copy before modify
 *   - pipelined precommit immediately after a subtrie is done with updates.
 *     this is through an upward pointing tree (child points to parent).
 *     calculate parent's data[i] when curr node has no children pending.
 *   - call upward_update_data() from poll() after async read.
 */
void merge_trie(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    trie_branch_node_t const *const tmp_parent, uint8_t const tmp_branch_i,
    unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode)
{
    assert(!(prev_parent->tomb_arr_mask & 1u << prev_child_i));
    uint8_t const new_branch_arr_i =
        merkle_child_index(new_parent, new_child_ni);
    unsigned char const prev_node_path_len =
        prev_parent->children[prev_child_i].path_len;
    unsigned char *const prev_node_path =
        prev_parent->children[prev_child_i].path;

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
            break;
        }
        // not reach the last nibble in current node yet
        unsigned char prev_nibble = get_nibble(prev_node_path, pi),
                      tmp_nibble = get_nibble(tmp_node->path, pi);
        if (prev_nibble == tmp_nibble) { // curr nibble matched
            ++pi;
            continue;
        }
        else {
            // prev_parent->children[prev_child_i] must be ext node
            assert(
                prev_parent->children[prev_child_i].path_len -
                        prev_parent->path_len >
                    1 &&
                prev_parent->children[prev_child_i].data);

            // curr nibble mismatch, create a new branch node with 2 children
            new_branch =
                get_new_merkle_node(1u << prev_nibble | 1u << tmp_nibble, pi);

            // new_branch -> prev_nibble
            unsigned int prev_idx = prev_nibble > tmp_nibble;
            assign_prev_child_to_new(
                prev_parent, prev_child_i, new_branch, prev_idx);

            // new_branch -> tmp_nibble
            set_merkle_child_from_tmp(new_branch, !prev_idx, tmp_node);

            // update new_parent's specific child
            new_parent->children[new_branch_arr_i] =
                (merkle_child_info_t){.next = new_branch, .path_len = pi};
            memcpy(
                new_parent->children[new_branch_arr_i].path,
                tmp_node->path,
                (1 + pi) / 2);
            hash_branch_extension(new_parent, new_branch_arr_i);
            new_parent->children[new_branch_arr_i].fnext =
                write_node(new_branch);
            if (new_parent->children[new_branch_arr_i].path_len >=
                CACHE_LEVELS) {
                free_node(new_branch);
                new_parent->children[new_branch_arr_i].next = NULL;
            }
            --parent_tnode->npending;
            return;
        }
    }
    tnode_t *branch_tnode = NULL;
    if (tmp_is_shorter == 1) { // prev_node could be leaf
        next_nibble = get_nibble(prev_node_path, pi);
        if (tmp_node->next[next_nibble] != 0) {
            // create a new branch same as tmp trie
            new_branch = get_new_merkle_node(tmp_node->subnode_bitmask, pi);
            branch_tnode = get_new_tnode(
                parent_tnode, new_child_ni, new_branch_arr_i, new_branch);

            // copy each subtrie in tmp_node to new_branch_i
            // in new trie except for next_nibble branch.
            for (int i = 0, child_idx = 0; i < 16; ++i) {
                if (tmp_node->next[i]) {
                    if (i != next_nibble) {
                        set_merkle_child_from_tmp(
                            new_branch, child_idx, get_node(tmp_node->next[i]));
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
                next_nibble,
                branch_tnode);
        }
        else { // no more next branch in tmp trie that matches the
            // nibble in prev trie
            // copy tmp_node to new_branch
            new_branch = get_new_merkle_node(
                tmp_node->subnode_bitmask | 1u << next_nibble, pi);
            unsigned child_idx = 0;
            for (int i = 0; i < 16; ++i) {
                if (new_branch->mask & 1u << i) {
                    if (tmp_node->next[i]) {
                        set_merkle_child_from_tmp(
                            new_branch,
                            child_idx++,
                            get_node(tmp_node->next[i]));
                    }
                    else {
                        assign_prev_child_to_new(
                            prev_parent, prev_child_i, new_branch, child_idx++);
                    }
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
                new_child_ni,
                parent_tnode);
            async_read_request(uring_data);
            return; // async callback will pickup when finished
        }
        // tmp may be a leaf
        next_nibble = get_nibble(tmp_node->path, pi);
        if (prev_node->valid_mask & 1u << next_nibble) {
            // same branch out at pi in new trie as in prev trie, except
            // for next_nibble should be left open for next level merge

            // create a new branch for the new trie
            new_branch = copy_merkle_node_except(prev_node, next_nibble);
            branch_tnode = get_new_tnode(
                parent_tnode, new_child_ni, new_branch_arr_i, new_branch);
            branch_tnode->npending = 1;
            merge_trie(
                prev_node,
                merkle_child_index(prev_node, next_nibble),
                tmp_parent,
                tmp_branch_i,
                pi + 1,
                new_branch,
                next_nibble,
                branch_tnode);
        }
        else { // prev is shorter, no more matched next for prev trie
            // branch out for both prev trie and tmp trie
            // create a new branch for the new trie
            new_branch = get_new_merkle_node(
                prev_node->valid_mask | 1u << next_nibble, pi);
            for (int i = 0, child_idx = 0; i < 16; ++i) {
                if ((new_branch->mask & 1u << i)) {
                    if (i != next_nibble) {
                        assign_prev_child_to_new(
                            prev_node,
                            merkle_child_index(prev_node, i),
                            new_branch,
                            child_idx++);
                    }
                    else {
                        set_merkle_child_from_tmp(
                            new_branch, child_idx++, tmp_node);
                    }
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
        if (tmp_node->type == LEAF &&
            ((trie_leaf_node_t *)tmp_node)->tombstone) {
            --parent_tnode->npending;
            new_parent->valid_mask &= ~(1u << new_child_ni);
            new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            return;
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
                    new_child_ni,
                    parent_tnode);
                async_read_request(uring_data);
                return; // async callback will pickup when finished
            }
            // do_merge update branch_tnode's node and npending
            branch_tnode = get_new_tnode(
                parent_tnode, new_child_ni, new_branch_arr_i, new_branch);
            new_branch = do_merge(prev_node, tmp_node, pi, branch_tnode);
        }
        new_path = prev_node_path;
        new_path_len = prev_node_path_len;
    }
    new_parent->children[new_branch_arr_i].next = new_branch;
    new_parent->children[new_branch_arr_i].path_len = new_path_len;
    memcpy(
        new_parent->children[new_branch_arr_i].path,
        new_path,
        (1 + new_path_len) / 2);

    if (new_branch) {
        new_branch->path_len = new_path_len;
        if (branch_tnode && branch_tnode->npending) {
            return;
        }
        // new_branch has 0 or 1 valid children
        unsigned nvalid = merkle_child_count_valid(new_branch);
        if (nvalid == 0) {
            new_parent->valid_mask &= ~(1u << new_child_ni);
            new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            new_parent->children[new_branch_arr_i].next = NULL;
            free_node(new_branch);
        }
        else if (nvalid == 1) {
            connect_only_grandchild(new_parent, new_branch_arr_i);
        }
        else {
            hash_branch_extension(new_parent, new_branch_arr_i);
            new_parent->children[new_branch_arr_i].fnext =
                write_node(new_branch);
            if (new_parent->children[new_branch_arr_i].path_len >=
                CACHE_LEVELS) {
                free_node(new_branch);
                new_parent->children[new_branch_arr_i].next = NULL;
            }
        }
    }
    else {
        // exact prefix match for leaf
        assert(pi == min_path_len && !tmp_is_shorter && tmp_node->type == LEAF);
        assert(prev_parent->children[prev_child_i].data);
        new_parent->children[new_branch_arr_i].data =
            prev_parent->children[prev_child_i].data;
        prev_parent->children[prev_child_i].data = NULL;
        hash_leaf(
            new_parent,
            new_branch_arr_i,
            (unsigned char *)&((trie_leaf_node_t *)tmp_node)->data);
    }
    --parent_tnode->npending;
    return;
}

void upward_update_data(tnode_t *curr_tnode)
{
    if (!curr_tnode) {
        return;
    }
    while (!curr_tnode->npending && curr_tnode->parent) {
        merkle_node_t *parent = curr_tnode->parent->node,
                      *curr = curr_tnode->node;
        uint8_t child_idx = curr_tnode->child_idx,
                child_ni = curr_tnode->child_ni;
        // TODO: dup of the logic in merge_trie
        unsigned nvalid = merkle_child_count_valid(curr);
        if (nvalid == 0) {
            parent->valid_mask &= ~(1u << child_ni);
            parent->tomb_arr_mask |= 1u << child_idx;
            parent->children[child_idx].next = NULL;
            free_node(curr);
        }
        else if (nvalid == 1) {
            connect_only_grandchild(parent, child_idx);
        }
        else {
            // ready to sum for curr->node and update data in parent
            hash_branch_extension(parent, curr_tnode->child_idx);
            parent->children[curr_tnode->child_idx].fnext =
                write_node(curr_tnode->node);
            if (parent->children[curr_tnode->child_idx].path_len >=
                CACHE_LEVELS) {
                free_node(curr);
                parent->children[child_idx].next = NULL;
            }
        }
        --curr_tnode->parent->npending;
        curr_tnode = curr_tnode->parent;
    }
}
