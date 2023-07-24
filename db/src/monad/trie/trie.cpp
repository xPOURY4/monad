#include <variant>

#include <monad/trie/encode_node.hpp>
#include <monad/trie/io_senders.hpp>
#include <monad/trie/trie.hpp>
#include <monad/trie/util.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void MerkleTrie::upward_update_data(tnode_t *curr_tnode)
{
    if (!curr_tnode) {
        return;
    }
    while (!curr_tnode->npending && curr_tnode->parent) {
        merkle_node_t *parent = curr_tnode->parent->node;
        auto *curr = curr_tnode->node;
        uint8_t child_idx = curr_tnode->child_idx,
                child_ni = curr_tnode->child_ni;
        // TODO: dup of the logic in merge_trie
        unsigned nvalid = merkle_child_count_valid(curr);
        if (nvalid == 0) {
            parent->valid_mask &= ~(1u << child_ni);
            parent->tomb_arr_mask |= 1u << child_idx;
            parent->children()[child_idx].next.reset();
        }
        else if (nvalid == 1) {
            connect_only_grandchild(parent, child_idx, is_account_);
        }
        else {
            // ready to sum for curr->node and update data in parent
            encode_branch_extension(parent, child_idx);
            if (io_) {
                auto written = async_write_node(curr_tnode->node);
                auto &child = parent->children()[curr_tnode->child_idx];
                child.set_fnext(written.offset_written_to);
                child.set_node_len_upper_bound(written.bytes_appended);
                if (parent->path_len && child.path_len() >= cache_levels_) {
                    parent->children()[child_idx].next.reset();
                }
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
    parent->children()[child_idx].set_path_len(path_len);
    std::memcpy(parent->children()[child_idx].path, path, (path_len + 1) / 2);
}

void MerkleTrie::build_new_trie(
    merkle_node_t *const parent, uint8_t const arr_idx,
    Request::unique_ptr_type updates)
{
    SubRequestInfo nextlevel;
    if (updates->is_leaf()) {
        set_child_path_n_len(parent, arr_idx, updates->get_path(), 64);
        encode_leaf(
            parent,
            arr_idx,
            updates->get_only_leaf().opt.value().val,
            is_account_);
        parent->children()[arr_idx].next.reset();
    }
    else {
        while (
            (updates = updates->split_into_subqueues(
                 std::move(updates), &nextlevel))) {
        }
        // copy path, and path len
        set_child_path_n_len(
            parent, arr_idx, nextlevel.get_path(), nextlevel.path_len);
        // reconstruct the underlying trie from each nextlevel update list
        merkle_node_ptr new_node =
            get_new_merkle_node(nextlevel.mask, nextlevel.path_len);
        for (uint16_t i = 0, child_idx = 0, bit = 1; i < 16; ++i, bit <<= 1) {
            if (nextlevel.mask & bit) {
                build_new_trie(
                    new_node.get(), child_idx++, std::move(nextlevel)[i]);
            }
        }
        // hash node and write to disk
        parent->children()[arr_idx].next = std::move(new_node);
        encode_branch_extension(parent, arr_idx);
        if (io_) {
            auto written =
                async_write_node(parent->children()[arr_idx].next.get());
            auto &child = parent->children()[arr_idx];
            child.set_fnext(written.offset_written_to);
            child.set_node_len_upper_bound(written.bytes_appended);
            if (parent->path_len && // parent could be root
                child.path_len() >= cache_levels_) {
                child.next.reset();
            }
        }
    }
}

// @param nextlevel: all updates pending on different children of prev_root
merkle_node_ptr MerkleTrie::do_update(
    merkle_node_t *const prev_root, SubRequestInfo &nextlevel,
    tnode_t *const curr_tnode, unsigned char const pi)
{
    // both prev_root and requests are branching out at pi
    MONAD_ASSERT(pi == prev_root->path_len);
    // construct current tnode, and connect to parent tnode
    merkle_node_ptr new_root = get_new_merkle_node(
        prev_root->valid_mask | nextlevel.mask, prev_root->path_len);
    curr_tnode->node = new_root.get();
    curr_tnode->npending = new_root->size();

    // Can do better with bit op loop
    for (uint16_t i = 0, bit = 1, child_idx = 0; i < 16; ++i, bit <<= 1) {
        if (prev_root->valid_mask & bit) {
            if (nextlevel.mask & bit) { // both have branches
                // new_root may be recreated during merge_trie
                nextlevel[i]->prev_parent = prev_root;
                nextlevel[i]->prev_child_i = merkle_child_index(prev_root, i);
                update_trie(
                    std::move(nextlevel)[i],
                    pi + 1,
                    new_root.get(),
                    i,
                    curr_tnode); // repsonsible to delete nextlevel[i]
            }
            else { // prev has branches, nextlevel does not
                merkle_child_info_t &prev_child =
                    prev_root->children()[merkle_child_index(prev_root, i)];
                new_root->children()[child_idx] = std::move(prev_child);
                // only 1 ref per node for now
                --curr_tnode->npending;
            }
            ++child_idx;
        }
        else if (nextlevel.mask & bit) { // prev no branch, tmp has
            // prev has no branch, nextlevel does
            // this case must be account creation (not deletion)
            build_new_trie(
                new_root.get(),
                child_idx++,
                std::move(nextlevel)[i]); // responsible to free nextlevel[i]
            --curr_tnode->npending;
        }
    }
    return new_root;
}

struct update_receiver
{
    MerkleTrie *trie;
    file_offset_t offset;
    Request::unique_ptr_type updates;
    merkle_node_t *new_parent;
    tnode_t *parent_tnode;
    uint16_t buffer_off;
    unsigned char pi;
    uint8_t new_child_ni;
    unsigned bytes_to_read;

    update_receiver(
        Request::unique_ptr_type _updates, unsigned char _pi,
        merkle_node_t *const _new_parent, uint8_t const _new_child_ni,
        tnode_t *_parent_tnode, MerkleTrie *_trie)
        : trie(_trie)
        , updates(std::move(_updates))
        , new_parent(_new_parent)
        , parent_tnode(_parent_tnode)
        , pi(_pi)
        , new_child_ni(_new_child_ni)
    {
        // prep uring data
        auto &child = updates->prev_parent->children()[updates->prev_child_i];
        file_offset_t node_offset = child.fnext();
        offset = round_down_align<DISK_PAGE_BITS>(node_offset);
        buffer_off = uint16_t(node_offset - offset);
        bytes_to_read = round_up_align<DISK_PAGE_BITS>(
            buffer_off + child.node_len_upper_bound());
    }

    void set_value(
        erased_connected_operation *rawstate,
        result<std::span<const std::byte>> _buffer)
    {
        assert(updates);
        // Re-adopt ownership of operation state
        erased_connected_operation_ptr state(rawstate);
        MONAD_ASSERT(_buffer);
        std::span<const std::byte> buffer = std::move(_buffer).assume_value();
        // construct the node from the read buffer
        auto &child = updates->prev_parent->children()[updates->prev_child_i];
        auto node_path_len = child.path_len();
        MONAD_ASSERT(buffer.size() >= buffer_off + node_path_len);
        merkle_node_ptr node = deserialize_node_from_buffer(
            (unsigned char *)buffer.data() + buffer_off, node_path_len);
        assert(node->size() > 1);
        assert(node->mask);

        child.next = std::move(node);

        // callback to update_trie() from where that request
        // left out
        trie->update_trie(
            std::move(updates), pi, new_parent, new_child_ni, parent_tnode);
        // upward update parent until parent has more than one
        // valid subnodes
        trie->upward_update_data(parent_tnode);
        // when state destructs, i/o buffer is released for reuse
    }
};
struct read_update_sender : read_single_buffer_sender
{
    read_update_sender(const update_receiver &receiver)
        : read_single_buffer_sender(
              receiver.offset, {(std::byte *)nullptr /*set by AsyncIO for us*/,
                                receiver.bytes_to_read})
    {
    }
};

/* @param updates: pending on node prev_parent->prev_child_i
 * @param pi: curr pi we traverse down the trie
 * @param prev_parent/prev_child_i: the prev node we are comparing at with
 * updates->pi is the path_len that has matched, also the next pi to check
 */
void MerkleTrie::update_trie(
    Request::unique_ptr_type updates, unsigned char pi,
    merkle_node_t *const new_parent, uint8_t const new_child_ni,
    tnode_t *parent_tnode)
{
    merkle_node_t *const prev_parent = updates->prev_parent;
    uint8_t const prev_child_i = updates->prev_child_i;
    merkle_node_t *prev_node = prev_parent->children()[prev_child_i].next.get();
    assert(!(prev_parent->tomb_arr_mask & 1u << prev_child_i));

    uint8_t const new_branch_arr_i =
        merkle_child_index(new_parent, new_child_ni);
    unsigned char const prev_path_len =
        prev_parent->children()[prev_child_i].path_len();
    unsigned char *const prev_path = prev_parent->children()[prev_child_i].path;

    merkle_node_ptr new_branch;
    tnode_t::unique_ptr_type branch_tnode;
    unsigned char next_nibble;
    SubRequestInfo nextlevel;

    // pi: is the next nibble idx in path that we're checking on
    while (1) {
        if (pi == 64) { // all previous nibbles matched and reach the leaf
            assert(updates->is_leaf());
            if (monad::mpt::is_deletion(updates->get_only_leaf())) {
                new_parent->valid_mask &= ~(1u << new_child_ni);
                new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            }
            else {
                // exact prefix match for leaf
                assert(prev_parent->children()[prev_child_i].data);
                new_parent->children()[new_branch_arr_i].data.swap(
                    prev_parent->children()[prev_child_i].data);
                new_parent->children()[new_branch_arr_i].set_data_len(
                    prev_parent->children()[prev_child_i].data_len());
                prev_parent->children()[prev_child_i].set_data_len(0);
                set_child_path_n_len(
                    new_parent, new_branch_arr_i, prev_path, prev_path_len);
                encode_leaf(
                    new_parent,
                    new_branch_arr_i,
                    updates->get_only_leaf().opt.value().val,
                    is_account_);
            }
            --parent_tnode->npending;
            updates.reset();
            return;
        }
        // if min_path_len == pi, all nibbles in prev_nodes are matched
        if (pi == prev_path_len) {
            // case 1. prev_path_len <= request path len, prev_node is not
            // leaf go down next level in prev trie along the next nibble in
            // read request
            if (!prev_node && io_) {
                updates->prev_parent = prev_parent;
                updates->prev_child_i = prev_child_i;
                update_receiver receiver(
                    std::move(updates),
                    pi,
                    new_parent,
                    new_child_ni,
                    parent_tnode,
                    this);
                read_update_sender sender(receiver);
                assert(receiver.offset < node_writer_->sender().offset());
                auto iostate =
                    io_->make_connected(std::move(sender), std::move(receiver));
                assert(iostate->receiver().updates);
                // TEMPORARY: Handle temporary i/o submission failure
                MONAD_ASSERT(iostate->initiate());
                // TEMPORARY UNTIL ALL THIS GETS BROKEN OUT: Release
                // management until i/o completes
                iostate.release();
                return;
            }
            // compare pending updates, and if possible, split at pi
            if ((updates = updates->split_into_subqueues(
                     std::move(updates), &nextlevel))) {
                // case 1.1. requests have longer prefix than prev_node
                next_nibble = get_nibble(updates->get_path(), pi);
                if (prev_node->valid_mask & 1u << next_nibble) {
                    // same branch out at pi in new trie as in prev trie, except
                    // for next_nibble should be left empty for next level merge
                    new_branch = copy_merkle_node_except(
                        prev_node, next_nibble, is_account_);
                    branch_tnode = get_new_tnode(
                        parent_tnode,
                        new_child_ni,
                        new_branch_arr_i,
                        new_branch.get());
                    branch_tnode->npending = 1;
                    // go down next level in prev trie
                    updates->prev_parent = prev_node;
                    updates->prev_child_i =
                        merkle_child_index(prev_node, next_nibble);
                    update_trie(
                        std::move(updates),
                        pi + 1,
                        new_branch.get(),
                        next_nibble,
                        branch_tnode.get()); // responsible for deleting updates
                }
                else { // prev is shorter, no matched branch for prev trie
                    // branch out for both prev trie and updates
                    // create a new branch for the new trie
                    new_branch = get_new_merkle_node(
                        prev_node->valid_mask | 1u << next_nibble, pi);
                    for (uint16_t i = 0, child_idx = 0, bit = 1; i < 16;
                         ++i, bit <<= 1) {
                        if ((new_branch->mask & bit)) {
                            if (i != next_nibble) {
                                assign_prev_child_to_new(
                                    prev_node,
                                    merkle_child_index(prev_node, i),
                                    new_branch.get(),
                                    child_idx++,
                                    is_account_);
                            }
                            else {
                                build_new_trie(
                                    new_branch.get(),
                                    child_idx++,
                                    std::move(updates));
                            }
                        }
                    }
                }
            }
            else { // case 1.2. if prev_path_len == updates path len
                branch_tnode = get_new_tnode(
                    parent_tnode,
                    new_child_ni,
                    new_branch_arr_i,
                    new_branch.get());
                new_branch =
                    do_update(prev_node, nextlevel, branch_tnode.get(), pi);
            }
            break;
        }
        else {
            if (!(updates = updates->split_into_subqueues(
                      std::move(updates), &nextlevel))) {
                // case 2. updates branchs out starting from pi,
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
                        new_branch.get());
                }
                // populate new_branch's children with each subqueue of
                // requests except for the `next_nibble` branch
                for (uint16_t i = 0, child_idx = 0, bit = 1; i < 16;
                     ++i, bit <<= 1) {
                    if (new_branch.get()->mask & bit) {
                        if (i != next_nibble) {
                            build_new_trie(
                                new_branch.get(),
                                child_idx++,
                                std::move(nextlevel)[i]);
                        }
                        else {
                            if (has_ni_branch) {
                                branch_tnode->npending = 1;
                                // move to next level update sublist under
                                // next_nibble
                                nextlevel[next_nibble]->prev_parent =
                                    prev_parent;
                                nextlevel[next_nibble]->prev_child_i =
                                    prev_child_i;
                                update_trie(
                                    std::move(nextlevel)[next_nibble],
                                    pi + 1,
                                    new_branch.get(),
                                    next_nibble,
                                    branch_tnode.get());
                                ++child_idx;
                            }
                            else {
                                assign_prev_child_to_new(
                                    prev_parent,
                                    prev_child_i,
                                    new_branch.get(),
                                    child_idx++,
                                    is_account_);
                            }
                        }
                    }
                }
                break;
            }
        }
        // not reach the last nibble in current node yet, continue comparing
        // for next nibble
        unsigned char prev_nibble = get_nibble(prev_path, pi),
                      tmp_nibble = get_nibble(updates->get_path(), pi);
        if (prev_nibble == tmp_nibble) { // curr nibble matched
            ++pi;
            continue;
        }
        else { // mismatch in the middle of a node rel path
            // prev_parent->children()[prev_child_i] must be ext node
            assert(
                prev_parent->children()[prev_child_i].path_len() -
                        prev_parent->path_len >
                    1 &&
                prev_parent->children()[prev_child_i].data);
            // curr nibble mismatch, create a new branch node with 2
            // children
            new_branch =
                get_new_merkle_node(1u << prev_nibble | 1u << tmp_nibble, pi);

            // new_branch -> prev_nibble
            unsigned int prev_idx = prev_nibble > tmp_nibble;
            assign_prev_child_to_new(
                prev_parent,
                prev_child_i,
                new_branch.get(),
                prev_idx,
                is_account_);
            // new_branch -> tmp_nibble
            build_new_trie(new_branch.get(), !prev_idx, std::move(updates));
            break;
        }
    }
    // update new_parent's specific child
    auto *new_branch_ptr = new_branch.get();
    new_parent->children()[new_branch_arr_i].next = std::move(new_branch);
    set_child_path_n_len(new_parent, new_branch_arr_i, prev_path, pi);
    if (new_branch_ptr != nullptr) {
        new_branch_ptr->path_len = pi;
        if (branch_tnode) {
            if (branch_tnode->npending) {
                // @Vicky: Why do we need to drop managed ownership of this?
                // Can't we store it somewhere for the i/o completion
                // callback to pick up?
                branch_tnode.release();
                return;
            }
        }
        // new_branch_ptr has 0 or 1 valid children
        unsigned nvalid = merkle_child_count_valid(new_branch_ptr);
        if (nvalid == 0) {
            new_parent->valid_mask &= ~(1u << new_child_ni);
            new_parent->tomb_arr_mask |= 1u << new_branch_arr_i;
            new_parent->children()[new_branch_arr_i].next.reset();
        }
        else if (nvalid == 1) {
            connect_only_grandchild(new_parent, new_branch_arr_i, is_account_);
        }
        else {
            encode_branch_extension(new_parent, new_branch_arr_i);
            if (io_) {
                auto written = async_write_node(new_branch_ptr);
                auto &child = new_parent->children()[new_branch_arr_i];
                child.set_fnext(written.offset_written_to);
                child.set_node_len_upper_bound(written.bytes_appended);
                if (new_parent->path_len && child.path_len() >= cache_levels_) {
                    new_parent->children()[new_branch_arr_i].next.reset();
                }
            }
        }
    }
    --parent_tnode->npending;
    return;
}

MerkleTrie::async_write_node_result
MerkleTrie::async_write_node(merkle_node_t *node)
{
    io_->poll_nonblocking(1);
    auto *sender = &node_writer_->sender();
    const auto size = get_disk_node_size(node);
    const async_write_node_result ret{
        sender->offset() + sender->written_buffer_bytes(), size};
    const auto remaining_bytes = sender->remaining_buffer_bytes();
    [[likely]] if (size <= remaining_bytes) {
        auto *where_to_serialize = sender->advance_buffer_append(size);
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer(
            (unsigned char *)where_to_serialize, node, size);
    }
    else {
        // renew write sender
        auto to_initiate = replace_node_writer_(remaining_bytes);
        sender = &node_writer_->sender();
        auto *where_to_serialize = (unsigned char *)sender->buffer().data();
        assert(where_to_serialize != nullptr);
        serialize_node_to_buffer(where_to_serialize, node, size);
        // Move the front of this into the tail of to_initiate
        auto *where_to_serialize2 =
            to_initiate->sender().advance_buffer_append(remaining_bytes);
        assert(where_to_serialize2 != nullptr);
        memcpy(where_to_serialize2, where_to_serialize, remaining_bytes);
        memmove(
            where_to_serialize,
            where_to_serialize + remaining_bytes,
            size - remaining_bytes);
        sender->advance_buffer_append(size - remaining_bytes);
        MONAD_ASSERT(to_initiate->initiate());
        // shall be recycled by the i/o receiver
        to_initiate.release();
    }
    return ret;
}

MerkleTrie::async_write_node_result
MerkleTrie::flush_and_write_new_root_node(merkle_node_t *root)
{
    io_->flush();
    if (!root->valid_mask) {
        return {INVALID_OFFSET, 0};
    }
    auto ret = async_write_node(root);
    // Round up with all bits zero
    auto *sender = &node_writer_->sender();
    auto written = sender->written_buffer_bytes();
    auto paddedup = round_up_align<DISK_PAGE_BITS>(written);
    const auto tozerobytes = paddedup - written;
    auto *tozero = sender->advance_buffer_append(tozerobytes);
    assert(tozero != nullptr);
    memset(tozero, 0, tozerobytes);
    auto to_initiate = replace_node_writer_();
    MONAD_ASSERT(to_initiate->initiate());
    // shall be recycled by the i/o receiver
    to_initiate.release();
    return ret;
}

MONAD_TRIE_NAMESPACE_END