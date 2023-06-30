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
    *(uint16_t *)write_pos = node->valid_mask;
    write_pos += SIZE_OF_SUBNODE_BITMASK;
    MONAD_ASSERT(merkle_child_count_valid(node) > 1);

    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        *(int64_t *)write_pos = node->children[i].fnext;
        write_pos += SIZE_OF_FILE_OFFSET;
        std::memcpy(write_pos, &(node->children[i].noderef), 32);
        write_pos += SIZE_OF_NODE_REF;

        *write_pos = node->children[i].path_len;
        write_pos += SIZE_OF_PATH_LEN;

        if (node->children[i].data) {
            MONAD_ASSERT(partial_path_len(node, i));
            *write_pos = node->children[i].data_len;
            MONAD_ASSERT(*write_pos == 32);
            write_pos += SIZE_OF_DATA_LEN;
            std::memcpy(
                write_pos, node->children[i].data, node->children[i].data_len);
            write_pos += node->children[i].data_len;
        }
        size_t const path_len_bytes =
            (node->children[i].path_len + 1) / 2 - node->path_len / 2;
        std::memcpy(
            write_pos,
            node->children[i].path + node->path_len / 2,
            path_len_bytes);
        write_pos += path_len_bytes;
    }
}

merkle_node_t *deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len)
{
    assert(node_path_len < 64);
    uint16_t const mask = *(uint16_t *)read_pos;
    read_pos += SIZE_OF_SUBNODE_BITMASK;
    merkle_node_t *node = get_new_merkle_node(mask, node_path_len);

    for (unsigned i = 0; i < node->nsubnodes; ++i) {
        node->children[i].fnext = *(int64_t *)read_pos;
        read_pos += SIZE_OF_FILE_OFFSET;

        std::memcpy(&(node->children[i].noderef), read_pos, 32);
        read_pos += SIZE_OF_NODE_REF;

        node->children[i].path_len = *read_pos;
        read_pos += SIZE_OF_PATH_LEN;

        if (partial_path_len(node, i)) {
            node->children[i].data_len = *read_pos;
            MONAD_ASSERT(node->children[i].data_len == 32);
            read_pos += SIZE_OF_DATA_LEN;
            node->children[i].data = static_cast<unsigned char *>(
                std::malloc(node->children[i].data_len));
            std::memcpy(
                node->children[i].data, read_pos, node->children[i].data_len);
            read_pos += node->children[i].data_len;
        }
        // read relative path from disk
        unsigned const path_len_bytes =
            (node->children[i].path_len + 1) / 2 - node->path_len / 2;
        std::memcpy(
            node->children[i].path + node->path_len / 2,
            read_pos,
            path_len_bytes);
        read_pos += path_len_bytes;
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
            unsigned char relpath[33];
            encode_two_piece(
                compact_encode(
                    relpath,
                    new_child->path,
                    new_parent->path_len + 1,
                    new_child->path_len,
                    new_child->path_len == 64),
                byte_string_view{new_child->data, new_child->data_len},
                new_child->noderef);
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
        sizeof(merkle_child_info_t) - 32);

    if (midnode->children[only_child_i].data) {
        midnode->children[only_child_i].data = nullptr;
    }
    else {
        assert(midnode->path_len + 1 == child->path_len);
        child->data = static_cast<unsigned char *>(std::malloc(32));
        std::memcpy(child->data, &child->noderef, 32);
        child->data_len = 32;
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
    unsigned char relpath[33];
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
    assert((child->next != nullptr) != (child->path_len >= CACHE_LEVELS));
    free_node(midnode);
}

MONAD_TRIE_NAMESPACE_END