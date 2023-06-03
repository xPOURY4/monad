#include <monad/trie/io.hpp>
#include <monad/trie/node_helper.hpp>

MONAD_TRIE_NAMESPACE_BEGIN

void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node)
{
    *(uint16_t *)write_pos = node->valid_mask;
    write_pos += SIZE_OF_SUBNODE_BITMASK;
    MONAD_TRIE_ASSERT(merkle_child_count_valid(node) > 1);

    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        *(int64_t *)write_pos = node->children[i].fnext;
        write_pos += SIZE_OF_FILE_OFFSET;
        std::memcpy(write_pos, &(node->children[i].noderef), 32);
        write_pos += SIZE_OF_TRIE_DATA;

        *write_pos = node->children[i].path_len;
        write_pos += SIZE_OF_PATH_LEN;

        if (node->children[i].data) {
            MONAD_TRIE_ASSERT(node->children[i].path_len - node->path_len > 1);
            std::memcpy(write_pos, node->children[i].data, SIZE_OF_TRIE_DATA);
            write_pos += SIZE_OF_TRIE_DATA;
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
        read_pos += SIZE_OF_TRIE_DATA;

        node->children[i].path_len = *read_pos;
        read_pos += SIZE_OF_PATH_LEN;

        if (node->children[i].path_len - node->path_len > 1) {
            node->children[i].data =
                static_cast<unsigned char *>(std::malloc(SIZE_OF_TRIE_DATA));
            std::memcpy(node->children[i].data, read_pos, SIZE_OF_TRIE_DATA);
            read_pos += SIZE_OF_TRIE_DATA;
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
    if (prev_parent->path_len < new_parent->path_len) {
        assert(prev_child->path_len - prev_parent->path_len > 1);
        if (new_child->path_len - new_parent->path_len == 1) {
            // prev_child is ext node, new_child is branch
            std::memcpy(&new_child->noderef, new_child->data, 32);
            free(new_child->data);
            new_child->data = nullptr;
            return;
        }
    }
    else if (prev_parent->path_len > new_parent->path_len) {
        if (prev_child->path_len - prev_parent->path_len == 1) {
            // prev_child is branch, new_child is ext
            assert(new_child->data == nullptr);
            new_child->data = static_cast<unsigned char *>(std::malloc(32));
            std::memcpy(new_child->data, &prev_child->noderef, 32);
        }
    }
    else {
        return;
    }
    hash_two_piece(
        new_child->path,
        new_parent->path_len + 1,
        new_child->path_len,
        new_child->path_len == 64,
        new_child->data,
        reinterpret_cast<unsigned char *>(&new_child->noderef));
}

void connect_only_grandchild(merkle_node_t *parent, uint8_t child_idx)
{
    merkle_node_t *midnode = parent->children[child_idx].next;
    uint8_t only_child_i =
        merkle_child_index(midnode, __builtin_ctz(midnode->valid_mask));
    unsigned mid_path_len = midnode->path_len;
    std::memcpy(
        &parent->children[child_idx],
        &midnode->children[only_child_i],
        sizeof(merkle_child_info_t) - 32);

    if (midnode->children[only_child_i].data) {
        midnode->children[only_child_i].data = nullptr;
    }
    else {
        assert(midnode->path_len + 1 == parent->children[child_idx].path_len);
        parent->children[child_idx].data =
            static_cast<unsigned char *>(std::malloc(32));
        std::memcpy(
            parent->children[child_idx].data,
            &parent->children[child_idx].noderef,
            32);
    }

    std::memcpy(
        parent->children[child_idx].path + (mid_path_len + 1) / 2,
        midnode->children[only_child_i].path + (mid_path_len + 1) / 2,
        (midnode->children[only_child_i].path_len + 1) / 2 -
            (mid_path_len + 1) / 2);
    if (mid_path_len % 2) { // odd path_len
        set_nibble(
            parent->children[child_idx].path,
            mid_path_len,
            get_nibble(midnode->children[only_child_i].path, mid_path_len));
    }
    // TODO: can optimize it: only recompute once
    hash_two_piece(
        parent->children[child_idx].path,
        parent->path_len + 1,
        parent->children[child_idx].path_len,
        parent->children[child_idx].path_len == 64,
        parent->children[child_idx].data,
        (unsigned char *)&parent->children[child_idx].noderef);
    assert(
        parent->children[child_idx].fnext ||
        parent->children[child_idx].path_len == 64);
    assert(
        (parent->children[child_idx].next != nullptr) !=
        parent->children[child_idx].path_len >= CACHE_LEVELS);
    free_node(midnode);
}

MONAD_TRIE_NAMESPACE_END