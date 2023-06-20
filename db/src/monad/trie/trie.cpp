#include <variant>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/trie.hpp>
#include <monad/trie/uring_data.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void update_callback(void *user_data)
{
    // construct the node from the read buffer
    update_uring_data_t *data = (update_uring_data_t *)user_data;
    merkle_node_t *node = deserialize_node_from_buffer(
        data->buffer + data->buffer_off,
        data->updates->prev_parent->children[data->updates->prev_child_i]
            .path_len);
    assert(node->nsubnodes > 1);
    assert(node->mask);

    data->updates->prev_parent->children[data->updates->prev_child_i].next =
        node;
    data->trie->get_io().release_read_buffer(data->buffer);

    // callback to update_trie() from where that request left out
    data->trie->update_trie(
        data->updates,
        data->pi,
        data->new_parent,
        data->new_child_ni,
        data->parent_tnode);
    // upward update parent until parent has more than one valid subnodes
    data->trie->upward_update_data(data->parent_tnode);
    update_uring_data_t::pool.destroy(data);
}

void MerkleTrie::upward_update_data(tnode_t *curr_tnode)
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
            parent->children[child_idx].next = nullptr;
            free_node(curr);
        }
        else if (nvalid == 1) {
            connect_only_grandchild(parent, child_idx);
        }
        else {
            // ready to sum for curr->node and update data in parent
            encode_branch_extension(parent, curr_tnode->child_idx);
            parent->children[curr_tnode->child_idx].fnext =
                io_.async_write_node(curr_tnode->node);
            if (parent->children[curr_tnode->child_idx].path_len >=
                cache_levels_) {
                free_node(curr);
                parent->children[child_idx].next = nullptr;
            }
        }
        --curr_tnode->parent->npending;
        tnode_t *p = curr_tnode->parent;
        tnode_t::unique_ptr_type{curr_tnode};
        curr_tnode = p;
    }
}

// set path and path len
void set_child_path_n_len(
    merkle_node_t *const parent, uint8_t const child_idx,
    unsigned char const *const path, uint8_t const path_len)
{
    parent->children[child_idx].path_len = path_len;
    std::memcpy(parent->children[child_idx].path, path, (path_len + 1) / 2);
}

void MerkleTrie::build_new_trie(
    merkle_node_t *const parent, uint8_t const arr_idx, Request *updates)
{
    SubRequestInfo nextlevel;
    if (updates->is_leaf()) {
        set_child_path_n_len(parent, arr_idx, updates->get_path(), 64);
        encode_leaf(parent, arr_idx, updates->get_only_leaf().opt.value().val);
        parent->children[arr_idx].next = nullptr;
        Request::pool.destroy(updates);
    }
    else {
        while ((updates = updates->split_into_subqueues(&nextlevel))) {
        }
        // copy path, and path len
        set_child_path_n_len(
            parent, arr_idx, nextlevel.get_path(), nextlevel.path_len);
        // copy the whole trie
        merkle_node_t *new_node =
            get_new_merkle_node(nextlevel.mask, nextlevel.path_len);
        for (int i = 0, child_idx = 0; i < 16; ++i) {
            if (nextlevel.mask & 1u << i) {
                build_new_trie(new_node, child_idx++, nextlevel[i]);
            }
        }
        // hash node and write to disk
        parent->children[arr_idx].next = new_node;
        encode_branch_extension(parent, arr_idx);
        parent->children[arr_idx].fnext = io_.async_write_node(new_node);
        if (parent->children[arr_idx].path_len >= cache_levels_) {
            free_node(parent->children[arr_idx].next);
            parent->children[arr_idx].next = nullptr;
        }
    }
}

// @param nextlevel: all updates pending on different children of prev_root
merkle_node_t *MerkleTrie::do_update(
    merkle_node_t *const prev_root, SubRequestInfo &nextlevel,
    tnode_t *const curr_tnode, unsigned char const pi)
{
    // both prev_root and requests are branching out at pi
    MONAD_ASSERT(pi == prev_root->path_len);
    merkle_node_t *new_root = get_new_merkle_node(
        prev_root->valid_mask | nextlevel.mask, prev_root->path_len);
    // construct current tnode, and connect to parent tnode
    curr_tnode->node = new_root;
    curr_tnode->npending = new_root->nsubnodes;

    // Can do better with bit op loop
    for (int i = 0, child_idx = 0; i < 16; ++i) {
        if (prev_root->valid_mask & 1u << i) {
            if (nextlevel.mask & 1u << i) { // both have branches
                // new_root may be recreated during merge_trie
                nextlevel[i]->prev_parent = prev_root;
                nextlevel[i]->prev_child_i = merkle_child_index(prev_root, i);
                update_trie(
                    nextlevel[i],
                    pi + 1,
                    new_root,
                    i,
                    curr_tnode); // repsonsible to delete updates[i]
            }
            else { // prev has branches, tmp do not
                merkle_child_info_t *prev_child =
                    &prev_root->children[merkle_child_index(prev_root, i)];
                new_root->children[child_idx] = *prev_child;
                // only 1 ref per node for now
                prev_child->next = nullptr;
                prev_child->data = nullptr;
                prev_child->data_len = 0;
                --curr_tnode->npending;
            }
            ++child_idx;
        }
        else if (nextlevel.mask & 1u << i) { // prev no branch, tmp has
            // this case must be account creation
            build_new_trie(
                new_root,
                child_idx++,
                nextlevel[i]); // responsible to free nextlevel[i]
            --curr_tnode->npending;
        }
    }
    return new_root;
}

/* @param updates: pending on node prev_parent->prev_child_i
 * @param pi: curr pi we traverse down the trie
 * @param prev_parent/prev_child_i: the prev node we are comparing at with
 * updates->pi is the path_len that has matched, also the next pi to check
 */
void MerkleTrie::update_trie(
    Request *updates, unsigned char pi, merkle_node_t *const new_parent,
    uint8_t const new_child_ni, tnode_t *parent_tnode)
{
    merkle_node_t *const prev_parent = updates->prev_parent;
    uint8_t const prev_child_i = updates->prev_child_i;
    merkle_node_t *prev_node = prev_parent->children[prev_child_i].next;
    assert(!(prev_parent->tomb_arr_mask & 1u << prev_child_i));

    uint8_t const new_branch_arr_i =
        merkle_child_index(new_parent, new_child_ni);
    unsigned char const prev_path_len =
        prev_parent->children[prev_child_i].path_len;
    unsigned char *const prev_path = prev_parent->children[prev_child_i].path;

    merkle_node_t *new_branch = nullptr;
    tnode_t::unique_ptr_type branch_tnode;
    unsigned char next_nibble;
    SubRequestInfo nextlevel;

    // pi: is the next path nibble id we're checking on
    while (1) {
        if (pi == 64) { // all previous nibbles matched and reach the leaf
            assert(updates->is_leaf());
            if (monad::mpt::is_deletion(updates->get_only_leaf())) {
                new_parent->valid_mask &= ~(1u << new_child_ni);
                new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            }
            else {
                // exact prefix match for leaf
                assert(prev_parent->children[prev_child_i].data);
                new_parent->children[new_branch_arr_i].data =
                    prev_parent->children[prev_child_i].data;
                new_parent->children[new_branch_arr_i].data_len =
                    prev_parent->children[prev_child_i].data_len;
                prev_parent->children[prev_child_i].data = nullptr;
                prev_parent->children[prev_child_i].data_len = 0;
                set_child_path_n_len(
                    new_parent, new_branch_arr_i, prev_path, prev_path_len);
                encode_leaf(
                    new_parent,
                    new_branch_arr_i,
                    updates->get_only_leaf().opt.value().val);
            }
            --parent_tnode->npending;
            Request::pool.destroy(updates);
            return;
        }
        // if min_path_len == pi, all nibbles in prev_nodes are matched
        if (pi == prev_path_len) {
            // case 1. prev_path_len <= request path len
            // go down to next level prev_node by a read request if not leaf
            if (!prev_node) {
                updates->prev_parent = prev_parent;
                updates->prev_child_i = prev_child_i;
                io_.async_read_request<
                    update_uring_data_t>(get_update_uring_data(
                    updates, pi, new_parent, new_child_ni, parent_tnode, this));
                return;
            }
            // compare pending updates, and if possible, split at pi
            if ((updates = updates->split_into_subqueues(&nextlevel))) {
                // case 1.1. requests have longer prefix than prev_node
                next_nibble = get_nibble(updates->get_path(), pi);
                if (prev_node->valid_mask & 1u << next_nibble) {
                    // same branch out at pi in new trie as in prev trie, except
                    // for next_nibble should be left open for next level merge
                    new_branch =
                        copy_merkle_node_except(prev_node, next_nibble);
                    branch_tnode = get_new_tnode(
                        parent_tnode,
                        new_child_ni,
                        new_branch_arr_i,
                        new_branch);
                    branch_tnode->npending = 1;
                    // go down next level in prev trie
                    updates->prev_parent = prev_node;
                    updates->prev_child_i =
                        merkle_child_index(prev_node, next_nibble);
                    update_trie(
                        updates,
                        pi + 1,
                        new_branch,
                        next_nibble,
                        branch_tnode.get()); // responsible for deleting updates
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
                                build_new_trie(
                                    new_branch, child_idx++, updates);
                            }
                        }
                    }
                }
            }
            else { // case 1.2. if prev_path_len == updates path len
                   // (not leaf)
                branch_tnode = get_new_tnode(
                    parent_tnode, new_child_ni, new_branch_arr_i, new_branch);
                new_branch =
                    do_update(prev_node, nextlevel, branch_tnode.get(), pi);
            }
            break;
        }
        else {
            if (!(updates = updates->split_into_subqueues(&nextlevel))) {
                // case 2. tmp_is_shorter, matched path_len is pi,
                // prev_node could be a leaf
                next_nibble = get_nibble(prev_path, pi);
                bool has_ni_branch = nextlevel.mask & 1u << next_nibble;
                new_branch = get_new_merkle_node(
                    has_ni_branch ? nextlevel.mask
                                  : (nextlevel.mask | 1u << next_nibble),
                    pi);
                if (has_ni_branch) {
                    branch_tnode = get_new_tnode(
                        parent_tnode,
                        new_child_ni,
                        new_branch_arr_i,
                        new_branch);
                }
                // populate new_branch's children with each subqueue of requests
                // except for the `next_nibble` branch
                for (int i = 0, child_idx = 0; i < 16; ++i) {
                    if (new_branch->mask & 1u << i) {
                        if (i != next_nibble) {
                            build_new_trie(
                                new_branch, child_idx++, nextlevel[i]);
                        }
                        else {
                            if (has_ni_branch) {
                                branch_tnode->npending = 1;
                                // move one level down on the tmp trie under
                                // next_nibble
                                nextlevel[next_nibble]->prev_parent =
                                    prev_parent;
                                nextlevel[next_nibble]->prev_child_i =
                                    prev_child_i;
                                update_trie(
                                    nextlevel[next_nibble],
                                    pi + 1,
                                    new_branch,
                                    next_nibble,
                                    branch_tnode.get());
                                ++child_idx;
                            }
                            else {
                                assign_prev_child_to_new(
                                    prev_parent,
                                    prev_child_i,
                                    new_branch,
                                    child_idx++);
                            }
                        }
                    }
                }
                break;
            }
        }
        // not reach the last nibble in current node yet, continue comparing for
        // next nibble
        unsigned char prev_nibble = get_nibble(prev_path, pi),
                      tmp_nibble = get_nibble(updates->get_path(), pi);
        if (prev_nibble == tmp_nibble) { // curr nibble matched
            ++pi;
            continue;
        }
        else { // mismatch in the middle of a node rel path
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
            build_new_trie(new_branch, !prev_idx, updates);
            break;
        }
    }
    // update new_parent's specific child
    new_parent->children[new_branch_arr_i].next = new_branch;
    set_child_path_n_len(new_parent, new_branch_arr_i, prev_path, pi);
    if (new_branch) {
        new_branch->path_len = pi;
        if (branch_tnode) {
            if (branch_tnode->npending) {
                branch_tnode.release();
                return;
            }
        }
        // new_branch has 0 or 1 valid children
        unsigned nvalid = merkle_child_count_valid(new_branch);
        if (nvalid == 0) {
            new_parent->valid_mask &= ~(1u << new_child_ni);
            new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            new_parent->children[new_branch_arr_i].next = nullptr;
            free_node(new_branch);
        }
        else if (nvalid == 1) {
            connect_only_grandchild(new_parent, new_branch_arr_i);
        }
        else {
            encode_branch_extension(new_parent, new_branch_arr_i);
            new_parent->children[new_branch_arr_i].fnext =
                io_.async_write_node(new_branch);
            if (new_parent->children[new_branch_arr_i].path_len >=
                cache_levels_) {
                free_node(new_branch);
                new_parent->children[new_branch_arr_i].next = nullptr;
            }
        }
    }
    --parent_tnode->npending;
    return;
}

MONAD_TRIE_NAMESPACE_END