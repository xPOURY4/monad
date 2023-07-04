#include <monad/core/unaligned.hpp>
#include <monad/trie/io.hpp>
#include <monad/trie/node_helper.hpp>

#if __GNUC__ == 12 && !defined(__clang__)
    #pragma GCC diagnostic ignored "-Warray-bounds" // is broken on GCC 12
    #pragma GCC diagnostic ignored "-Wstringop-overread" // is broken on GCC 12
#endif

MONAD_TRIE_NAMESPACE_BEGIN

// TODO: store leaf data separate from its parent.
void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node)
{
    auto write_item = [&](void *p, const auto &i) {
        using type = std::decay_t<decltype(i)>;
        std::memcpy(p, &i, sizeof(type));
        write_pos += sizeof(type);
    };

    auto write_item_len = [&](void *p, const auto *i, const uint8_t len) {
        std::memcpy(p, i, len);
        write_pos += len;
    };

    write_item(write_pos, node->valid_mask);
    MONAD_DEBUG_ASSERT(
        merkle_child_count_valid(node) >= 1); // root can have 1 child

    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        merkle_child_info_t const &child = node->children[i];
        write_item(write_pos, child.fnext);
        write_item_len(
            write_pos, &child.noderef, sizeof(merkle_child_info_t::noderef_t));

        *write_pos = child.path_len;
        write_pos += sizeof(merkle_child_info_t::path_len_t);

        if (child.data) {
            MONAD_DEBUG_ASSERT(
                partial_path_len(node, i) || child.path_len == 64);
            *write_pos = child.data_len;
            write_pos += sizeof(merkle_child_info_t::data_len_t);
            write_item_len(write_pos, child.data, child.data_len);
        }
        write_item_len(
            write_pos,
            child.path + node->path_len / 2,
            (child.path_len + 1) / 2 - node->path_len / 2);
    }
}

merkle_node_t *deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len)
{
    assert(node_path_len < 64);

    merkle_node_t::mask_t const mask =
        unaligned_load<merkle_node_t::mask_t>(read_pos);
    read_pos += sizeof(merkle_node_t::mask_t);
    merkle_node_t *node = get_new_merkle_node(mask, node_path_len);

    auto read_item = [&](auto &i, void const *p) {
        using type = std::decay_t<decltype(i)>;
        std::memcpy(&i, p, sizeof(type));
        read_pos += sizeof(type);
    };

    auto read_item_len = [&](auto *const i, void const *p, const uint8_t len) {
        std::memcpy(i, p, len);
        read_pos += len;
    };

    for (unsigned i = 0; i < node->nsubnodes; ++i) {
        merkle_child_info_t &child = node->children[i];
        read_item(child.fnext, read_pos);
        read_item_len(
            &child.noderef, read_pos, sizeof(merkle_child_info_t::noderef));

        child.path_len = *read_pos;
        read_pos += sizeof(merkle_child_info_t::path_len_t);

        if (partial_path_len(node, i) || child.path_len == 64) {
            child.data_len = *read_pos;
            read_pos += sizeof(merkle_child_info_t::data_len_t);
            child.data =
                static_cast<unsigned char *>(std::malloc(child.data_len));
            MONAD_ASSERT(child.data != nullptr);
            read_item_len(child.data, read_pos, child.data_len);
        }
        // read relative path from disk
        read_item_len(
            child.path + node->path_len / 2,
            read_pos,
            (child.path_len + 1) / 2 - node->path_len / 2);
    }
    return node;
}

void free_trie(merkle_node_t *const node)
{
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].data) {
            free(node->children[i].data);
        }
        if (node->children[i].next) {
            free_trie(node->children[i].next);
        }
    }
    free(node);
}

// parent path_len: always start writing from path_len / 2
void assign_prev_child_to_new(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    merkle_node_t *const new_parent, uint8_t const new_child_i)
{
    merkle_child_info_t *new_child = &new_parent->children[new_child_i],
                        *prev_child = &prev_parent->children[prev_child_i];
    *new_child = *prev_child;
    prev_child->data = nullptr;
    prev_child->next = nullptr;
    MONAD_ASSERT(prev_parent->path_len <= new_parent->path_len);
    if (prev_parent->path_len < new_parent->path_len) {
        assert(partial_path_len(prev_parent, prev_child_i));
        if (!partial_path_len(new_parent, new_child_i)) {
            // prev_child is ext node, new_child is branch (not leaf)
            MONAD_ASSERT(new_child->path_len < 64);
            std::memcpy(
                &new_child->noderef, new_child->data, new_child->data_len);
            free(new_child->data);
            new_child->data = nullptr;
            new_child->data_len = 0;
        }
        else {
            unsigned char relpath[sizeof(merkle_child_info_t::noderef_t) + 1];
            encode_two_piece(
                compact_encode(
                    relpath,
                    new_child->path,
                    new_parent->path_len + 1,
                    new_child->path_len,
                    new_child->path_len == 64),
                byte_string_view{new_child->data, new_child->data_len},
                new_child->noderef.data());
        }
    }
}

void connect_only_grandchild(merkle_node_t *parent, uint8_t child_idx)
{
    merkle_child_info_t *child = &parent->children[child_idx];
    merkle_node_t *midnode = child->next;
    uint8_t only_child_i =
        merkle_child_index(midnode, std::countr_zero(midnode->valid_mask));
    unsigned mid_path_len = midnode->path_len;
    std::memcpy(
        &parent->children[child_idx],
        &midnode->children[only_child_i],
        sizeof(merkle_child_info_t) - sizeof(merkle_child_info_t::noderef_t));

    if (midnode->children[only_child_i].data) {
        midnode->children[only_child_i].data = nullptr;
    }
    else {
        assert(midnode->path_len + 1 == child->path_len);
        child->data = static_cast<unsigned char *>(
            std::malloc(sizeof(merkle_child_info_t::noderef_t)));
        MONAD_ASSERT(child->data != nullptr);
        std::memcpy(
            child->data,
            &child->noderef,
            sizeof(merkle_child_info_t::noderef_t));
        child->data_len = sizeof(merkle_child_info_t::noderef_t);
    }

    std::memcpy(
        child->path + (mid_path_len + 1) / 2,
        midnode->children[only_child_i].path + (mid_path_len + 1) / 2,
        (midnode->children[only_child_i].path_len + 1) / 2 -
            (mid_path_len + 1) / 2);
    if (mid_path_len % 2) { // odd path_len
        set_nibble(
            child->path,
            mid_path_len,
            get_nibble(midnode->children[only_child_i].path, mid_path_len));
    }
    // TODO: can optimize it: only recompute once
    unsigned char relpath[sizeof(merkle_child_info_t::noderef_t) + 1];
    encode_two_piece(
        compact_encode(
            relpath,
            child->path,
            parent->path_len + 1,
            child->path_len,
            child->path_len == 64),
        byte_string_view{child->data, child->data_len},
        (unsigned char *)&child->noderef);
    assert(child->fnext || child->path_len == 64);
    free_node(midnode);
}

MONAD_TRIE_NAMESPACE_END