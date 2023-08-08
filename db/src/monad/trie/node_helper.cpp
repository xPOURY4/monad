#include <monad/core/unaligned.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/node_helper.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

// TODO: store leaf data separate from its parent.
void serialize_node_to_buffer(
    unsigned char *const write_pos_, merkle_node_t const *const node,
    unsigned shouldbe_bytes_written)
{
    unsigned char *write_pos = write_pos_;
    auto write_item = [&](void *p, const auto &i) {
        std::memcpy(p, &i, sizeof(i));
        write_pos += sizeof(i);
    };

    auto write_item_len = [&](void *p, const auto *i, const uint8_t len) {
        std::memcpy(p, i, len);
        write_pos += len;
    };

    write_item(write_pos, node->valid_mask);
    MONAD_DEBUG_ASSERT(merkle_child_count_valid(node) >= 1);

    for (uint16_t i = 0, bit = 1; i < node->size(); ++i, bit <<= 1) {
        if (node->tomb_arr_mask & bit) {
            continue;
        }
        merkle_child_info_t const &child = node->children()[i];
        write_item(write_pos, child.bitpacked);
        write_item(write_pos, child.noderef_len);
        write_item_len(write_pos, &child.noderef, child.noderef_len);

        if (child.data) {
            MONAD_DEBUG_ASSERT(
                partial_path_len(node, i) || child.path_len() == 64);
            write_item_len(write_pos, child.data.get(), child.data_len());
        }
        write_item_len(
            write_pos,
            child.path + node->path_len / 2,
            (child.path_len() + 1) / 2 - node->path_len / 2);
    }
    shouldbe_bytes_written -= write_pos - write_pos_;
    // If this trips, get_disk_node_size() does not match this routine.
    assert(shouldbe_bytes_written < 2);
    std::memset(write_pos, 0, shouldbe_bytes_written);
}

merkle_node_ptr deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len)
{
    assert(node_path_len < 64);
    // Tell the compiler it can hard assume two byte alignment
    if (((uintptr_t)read_pos & 1) != 0) {
        assert(false);
        __builtin_unreachable();
    }

    merkle_node_t::mask_t const mask =
        unaligned_load<merkle_node_t::mask_t>(read_pos);
    read_pos += sizeof(merkle_node_t::mask_t);
    merkle_node_ptr node = get_new_merkle_node(mask, node_path_len);

    auto read_item = [&](auto &i, void const *p) {
        using type = std::decay_t<decltype(i)>;
        std::memcpy(&i, p, sizeof(type));
        read_pos += sizeof(type);
    };

    auto read_item_len = [&](auto *const i, void const *p, const uint8_t len) {
        std::memcpy(i, p, len);
        read_pos += len;
    };

    for (unsigned i = 0; i < node->size(); ++i) {
        merkle_child_info_t &child = node->children()[i];
        read_item(child.bitpacked, read_pos);
        child.set_noderef_len(
            unaligned_load<merkle_child_info_t::data_len_t>(read_pos));
        read_pos += sizeof(merkle_child_info_t::data_len_t);
        read_item_len(&child.noderef, read_pos, child.noderef_len);

        if (partial_path_len(node.get(), i) || child.path_len() == 64) {
            child.data = allocators::make_resizeable_unique_for_overwrite<
                unsigned char[]>(child.data_len());
            read_item_len(child.data.get(), read_pos, child.data_len());
        }
        // read relative path from disk
        read_item_len(
            child.path + node->path_len / 2,
            read_pos,
            (child.path_len() + 1) / 2 - node->path_len / 2);
    }
    return node;
}

// parent path_len: always start writing from path_len / 2
void assign_prev_child_to_new(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    merkle_node_t *const new_parent, uint8_t const new_child_i,
    bool const is_account)
{
    merkle_child_info_t *new_child = &new_parent->children()[new_child_i],
                        *prev_child = &prev_parent->children()[prev_child_i];
    new_child->copy_or_swap(*prev_child);
    MONAD_ASSERT(prev_parent->path_len <= new_parent->path_len);
    if (prev_parent->path_len < new_parent->path_len) {
        assert(partial_path_len(prev_parent, prev_child_i));
        if (!partial_path_len(new_parent, new_child_i)) {
            // prev_child is ext node, new_child is branch (not leaf)
            MONAD_ASSERT(new_child->path_len() < 64);
            std::memcpy(
                &new_child->noderef,
                new_child->data.get(),
                new_child->data_len());
            new_child->set_noderef_len(new_child->data_len());
            new_child->data.reset();
            new_child->set_data_len(0);
        }
        else {
            unsigned char relpath[sizeof(merkle_child_info_t::noderef_t) + 1];
            uint64_t noderef_len = encode_two_piece(
                compact_encode(
                    relpath,
                    new_child->path,
                    new_parent->path_len + 1,
                    new_child->path_len(),
                    new_child->path_len() == 64),
                byte_string_view{new_child->data.get(), new_child->data_len()},
                (new_child->path_len() == 64 && is_account) ? ROOT_OFFSET_SIZE
                                                            : 0,
                new_child->noderef.data(),
                new_child->path_len() == 64);
            new_child->set_noderef_len(noderef_len);
        }
    }
}

void connect_only_grandchild(
    merkle_node_t *parent, uint8_t child_idx, bool const is_account)
{
    merkle_child_info_t *child = &parent->children()[child_idx];
    unsigned char midnode_path[sizeof(merkle_child_info_t::path)];
    memcpy(midnode_path, child->path, sizeof(merkle_child_info_t::path));
    merkle_node_t *midnode = child->next.get();
    uint8_t only_child_i =
        merkle_child_index(midnode, std::countr_zero(midnode->valid_mask));
    unsigned mid_path_len = midnode->path_len;
    parent->children()[child_idx].copy_or_swap(
        midnode->children()[only_child_i]);
    memcpy(child->path, midnode_path, sizeof(merkle_child_info_t::path));
    if (!parent->children()[child_idx].data) {
        assert(midnode->path_len + 1 == child->path_len());
        child->data =
            allocators::make_resizeable_unique_for_overwrite<unsigned char[]>(
                sizeof(child->noderef_len));
        std::memcpy(child->data.get(), &child->noderef, child->noderef_len);
        child->set_data_len(child->noderef_len);
    }

    std::memcpy(
        child->path + (mid_path_len + 1) / 2,
        midnode->children()[only_child_i].path + (mid_path_len + 1) / 2,
        (midnode->children()[only_child_i].path_len() + 1) / 2 -
            (mid_path_len + 1) / 2);
    if (mid_path_len % 2) { // odd path_len
        set_nibble(
            child->path,
            mid_path_len,
            get_nibble(midnode->children()[only_child_i].path, mid_path_len));
    }
    // TODO: can optimize it: only recompute once
    unsigned char relpath[sizeof(merkle_child_info_t::path) + 1];
    uint64_t noderef_len = encode_two_piece(
        compact_encode(
            relpath,
            child->path,
            parent->path_len + 1,
            child->path_len(),
            child->path_len() == 64),
        byte_string_view{child->data.get(), child->data_len()},
        child->path_len() == 64 && is_account ? ROOT_OFFSET_SIZE : 0,
        child->noderef.data(),
        child->path_len() == 64);
    child->set_noderef_len(noderef_len);
    assert(child->fnext() > 0 || child->path_len() == 64);
}

MONAD_TRIE_NAMESPACE_END