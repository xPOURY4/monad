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
        // copy data
        copy_trie_data(
            &parent->children[arr_idx].data,
            &((trie_leaf_node_t *)tmp_node)->data);
        parent->children[arr_idx].next = NULL;
    }
    else {
        // copy the whole trie
        assert(tmp_node->type == BRANCH);
        merkle_node_t *new_node =
            get_new_merkle_node(tmp_node->subnode_bitmask, tmp_node->path_len);

        unsigned int child_idx = 0;
        // compute keccak
        unsigned char bytes[32 * tmp_node->nsubnodes];
        uint64_t b_offset = 0;
        for (int i = 0; i < 16; ++i) {
            if (tmp_node->next[i]) {
                set_merkle_child_from_tmp(
                    new_node, child_idx, get_node(tmp_node->next[i]));
                copy_trie_data(
                    (trie_data_t *)(bytes + b_offset),
                    &new_node->children[child_idx].data);
                b_offset += 32;
                ++child_idx;
            }
        }
        parent->children[arr_idx].next = new_node;
        copy_trie_data(
            &parent->children[arr_idx].data,
            (trie_data_t *)ethash_keccak256((uint8_t *)bytes, b_offset).str);
        parent->children[arr_idx].fnext = write_node(new_node);

        if (parent->children[arr_idx].path_len >= CACHE_LEVELS) {
            free_trie(parent->children[arr_idx].next);
            parent->children[arr_idx].next = NULL;
        }
    }
}

// parent path_len: always start writing from path_len / 2
void serialize_node_to_buffer(
    unsigned char *write_pos, merkle_node_t const *const node)
{
    *(uint16_t *)write_pos = node->mask;
    write_pos += SIZE_OF_SUBNODE_BITMASK;

    for (int i = 0; i < node->nsubnodes; ++i) {
        *(int64_t *)write_pos = node->children[i].fnext;
        write_pos += SIZE_OF_FILE_OFFSET;
        copy_trie_data((trie_data_t *)write_pos, &(node->children[i].data));
        write_pos += SIZE_OF_TRIE_DATA;

        *write_pos = node->children[i].path_len;
        write_pos += SIZE_OF_PATH_LEN;

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
    merkle_node_t *node = get_new_merkle_node(mask, 0); // set path_len later
    node->mask = mask;
    node->nsubnodes = __builtin_popcount(mask);
    node->path_len = node_path_len;

    for (unsigned i = 0; i < node->nsubnodes; ++i) {
        node->children[i].fnext = *(int64_t *)read_pos;
        read_pos += SIZE_OF_FILE_OFFSET;

        copy_trie_data(&(node->children[i].data), (trie_data_t *)read_pos);
        read_pos += SIZE_OF_TRIE_DATA;

        node->children[i].path_len = *read_pos;
        read_pos += SIZE_OF_PATH_LEN;

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
    if (size + buffer_idx > WRITE_BUFFER_SIZE) {
        unsigned char *prev_buffer = write_buffer;
        int64_t prev_block_off = block_off;
        // renew buffer
        block_off += WRITE_BUFFER_SIZE;
        write_buffer = get_avail_buffer(WRITE_BUFFER_SIZE);
        *write_buffer = BLOCK_TYPE_DATA;
        buffer_idx = 1;
        // buffer will be freed after iouring completed
        async_write_request(prev_buffer, prev_block_off);
    }
    // Write the root node to the buffer
    int64_t ret = block_off + buffer_idx;
    serialize_node_to_buffer(write_buffer + buffer_idx, node);
    buffer_idx += size;
    return ret;
}

uint64_t sum_data_first_word(merkle_node_t *const node)
{
    uint64_t sum_data = 0;
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (!node->children[i].data.words[0]) {
            printf("wrong data\n");
        }
        sum_data += node->children[i].data.words[0];
    }
    return sum_data;
}

void rehash_keccak(
    merkle_node_t *const parent, uint8_t const child_idx,
    merkle_node_t *const node)
{
    unsigned char bytes[node->nsubnodes * 32];
    uint32_t b_offset = 0;

    for (int i = 0; i < node->nsubnodes; ++i) {
        copy_trie_data(
            (trie_data_t *)(bytes + b_offset), &node->children[i].data);
        b_offset += 32;
    }
    copy_trie_data(
        &parent->children[child_idx].data,
        (trie_data_t *)ethash_keccak256((uint8_t *)bytes, b_offset).str);
    return;
}

void free_trie(merkle_node_t *const node)
{
    for (int i = 0; i < node->nsubnodes; ++i) {
        if (node->children[i].next) {
            free_trie(node->children[i].next);
            node->children[i].next = NULL;
        }
    }
    free(node);
}