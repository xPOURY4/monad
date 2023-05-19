#include <ethash/keccak.h>
#include <malloc.h>
#include <monad/merkle/merge.h>
#include <monad/merkle/node.h>
#include <monad/trie/io.h>

bool disas_merkle_child_test(merkle_node_t const *const node, unsigned const i)
{
    return merkle_child_test(node, i);
}

bool disas_merkle_child_all(merkle_node_t const *const node)
{
    return merkle_child_all(node);
}

bool disas_merkle_child_any(merkle_node_t const *const node)
{
    return merkle_child_any(node);
}

bool disas_merkle_child_none(merkle_node_t const *const node)
{
    return merkle_child_none(node);
}

unsigned disas_merkle_child_count(merkle_node_t const *const node)
{
    return merkle_child_count(node);
}

unsigned
disas_merkle_child_index(merkle_node_t const *const node, unsigned const i)
{
    return merkle_child_index(node, i);
}

// Copy the temporary trie to a new merkle trie of parent
// assign correct parent->children[arr_idx] values
// presumption: tmp_node trie are newly created accounts, no tombstone
void set_merkle_child_from_tmp(
    merkle_node_t *const parent, uint8_t const arr_idx,
    trie_branch_node_t const *const tmp_node)
{
    // copy path, and path len
    parent->children[arr_idx].path_len = tmp_node->path_len;
    memcpy(
        parent->children[arr_idx].path,
        tmp_node->path,
        (tmp_node->path_len + 1) / 2);

    if (tmp_node->type == LEAF) {
        parent->children[arr_idx].data = (unsigned char *)malloc(32);
        hash_leaf(
            parent,
            arr_idx,
            (unsigned char *)&((trie_leaf_node_t *)tmp_node)->data);
        parent->children[arr_idx].next = NULL;
    }
    else {
        // copy the whole trie
        merkle_node_t *new_node =
            get_new_merkle_node(tmp_node->subnode_bitmask, tmp_node->path_len);

        for (int i = 0, child_idx = 0; i < 16; ++i) {
            if (tmp_node->next[i]) {
                set_merkle_child_from_tmp(
                    new_node, child_idx++, get_node(tmp_node->next[i]));
            }
        }
        parent->children[arr_idx].next = new_node;
        hash_branch_extension(parent, arr_idx);
        parent->children[arr_idx].fnext = write_node(new_node);

        if (parent->children[arr_idx].path_len >= CACHE_LEVELS) {
            free_node(parent->children[arr_idx].next);
            parent->children[arr_idx].next = NULL;
        }
    }
}

// parent path_len: always start writing from path_len / 2
void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node)
{
    *(uint16_t *)write_pos = node->valid_mask;
    write_pos += SIZE_OF_SUBNODE_BITMASK;
    assert(merkle_child_count_valid(node) > 1);

    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->tomb_arr_mask & 1u << i) {
            continue;
        }
        *(int64_t *)write_pos = node->children[i].fnext;
        write_pos += SIZE_OF_FILE_OFFSET;
        copy_trie_data((trie_data_t *)write_pos, &(node->children[i].noderef));
        write_pos += SIZE_OF_TRIE_DATA;

        *write_pos = node->children[i].path_len;
        write_pos += SIZE_OF_PATH_LEN;

        if (node->children[i].data) {
            assert(node->children[i].path_len - node->path_len > 1);
            memcpy(write_pos, node->children[i].data, SIZE_OF_TRIE_DATA);
            write_pos += SIZE_OF_TRIE_DATA;
        }
        size_t const path_len_bytes =
            (node->children[i].path_len + 1) / 2 - node->path_len / 2;
        memcpy(
            write_pos,
            node->children[i].path + node->path_len / 2,
            path_len_bytes);
        write_pos += path_len_bytes;
    }
}

merkle_node_t *deserialize_node_from_buffer(
    unsigned char const *read_pos, unsigned char const node_path_len)
{
    uint16_t const mask = *(uint16_t *)read_pos;
    read_pos += SIZE_OF_SUBNODE_BITMASK;
    merkle_node_t *node = get_new_merkle_node(mask, node_path_len);

    for (unsigned i = 0; i < node->nsubnodes; ++i) {
        node->children[i].fnext = *(int64_t *)read_pos;
        read_pos += SIZE_OF_FILE_OFFSET;

        copy_trie_data(&(node->children[i].noderef), (trie_data_t *)read_pos);
        read_pos += SIZE_OF_TRIE_DATA;

        node->children[i].path_len = *read_pos;
        read_pos += SIZE_OF_PATH_LEN;

        if (node->children[i].path_len - node->path_len > 1) {
            node->children[i].data = (unsigned char *)malloc(SIZE_OF_TRIE_DATA);
            memcpy(node->children[i].data, read_pos, SIZE_OF_TRIE_DATA);
            read_pos += SIZE_OF_TRIE_DATA;
        }
        // read relative path from disk
        unsigned const path_len_bytes =
            (node->children[i].path_len + 1) / 2 - node->path_len / 2;
        memcpy(
            node->children[i].path + node->path_len / 2,
            read_pos,
            path_len_bytes);
        read_pos += path_len_bytes;
    }
    return node;
}

int64_t write_node(merkle_node_t *const node)
{
    size_t size = get_disk_node_size(node);
    while (size + BUFFER_IDX > WRITE_BUFFER_SIZE) {
        unsigned char *prev_buffer = WRITE_BUFFER;
        int64_t prev_block_off = BLOCK_OFF;
        // renew buffer
        BLOCK_OFF += WRITE_BUFFER_SIZE;
        WRITE_BUFFER = get_avail_buffer(WRITE_BUFFER_SIZE);
        *WRITE_BUFFER = BLOCK_TYPE_DATA;
        BUFFER_IDX = 1;
        // during poll_uring(), there could be new writes to WRITE_BUFFER, has
        // to be available before calling write_request(), buffer will be freed
        // after iouring completed
        async_write_request(prev_buffer, prev_block_off);
        // after async and polling, there might be insufficient space for new
        // node, put it in a while loop to submit both current and new buffer
    }
    // Write the root node to the buffer
    int64_t ret = BLOCK_OFF + BUFFER_IDX;
    serialize_node_to_buffer(WRITE_BUFFER + BUFFER_IDX, node);
    BUFFER_IDX += size;
    return ret;
}

void assign_prev_child_to_new(
    merkle_node_t *const prev_parent, uint8_t const prev_child_i,
    merkle_node_t *const new_parent, uint8_t const new_child_i)
{
    merkle_child_info_t *new_child = &new_parent->children[new_child_i],
                        *prev_child = &prev_parent->children[prev_child_i];
    *new_child = *prev_child;
    prev_child->data = NULL;
    prev_child->next = NULL;
    if (prev_parent->path_len < new_parent->path_len) {
        assert(prev_child->path_len - prev_parent->path_len > 1);
        if (new_child->path_len - new_parent->path_len == 1) {
            // prev_child is ext node, new_child is branch
            memcpy(&new_child->noderef, new_child->data, 32);
            free(new_child->data);
            new_child->data = NULL;
            return;
        }
    }
    else if (prev_parent->path_len > new_parent->path_len) {
        if (prev_child->path_len - prev_parent->path_len == 1) {
            // prev_child is branch, new_child is ext
            assert(new_child->data == NULL);
            new_child->data = (unsigned char *)malloc(32);
            memcpy(new_child->data, &prev_child->noderef, 32);
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
        (unsigned char *)&new_child->noderef);
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