#include <monad/merkle/node.h>

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

void set_merkle_child(
    merkle_node_t *const parent, uint8_t const arr_idx,
    trie_branch_node_t const *const tmp_node)
{
    if (tmp_node->type == LEAF) {
        // copy data
        copy_trie_data(
            &parent->children[arr_idx].data,
            &((trie_leaf_node_t *)tmp_node)->data);
        parent->children[arr_idx].next = NULL;
    }
    else {
        // copy the whole trie but not data
        parent->children[arr_idx].next = copy_tmp_trie(tmp_node, 0);
        assert(parent->children[arr_idx].next != NULL);
    }
    // copy path, and path len
    parent->children[arr_idx].path_len = tmp_node->path_len;
    memcpy(
        parent->children[arr_idx].path,
        tmp_node->path,
        (tmp_node->path_len + 1) / 2);
}
/*
    Copy the temporary trie to a new merkle trie
    new_node stores node's children data
    presumption: node is a branch node
*/
merkle_node_t *
copy_tmp_trie(trie_branch_node_t const *const node, uint16_t const mask)
{
    merkle_node_t *new_node =
        get_new_merkle_node(mask > 0 ? mask : node->subnode_bitmask);

    unsigned int child_idx = 0;
    for (int i = 0; i < 16; ++i) {
        if (new_node->mask & 1u << i) {
            if (node->next[i]) {
                trie_branch_node_t *const next_node = get_node(node->next[i]);
                set_merkle_child(new_node, child_idx, next_node);
            }
            ++child_idx;
        }
    }
    return new_node;
}

unsigned char *
write_node_to_buffer(unsigned char *write_pos, merkle_node_t const *const node)
{
    // Copy the mask of node
    *(uint16_t *)write_pos = node->mask;
    write_pos += SIZE_OF_SUBNODE_BITMASK;

    *(int8_t *)write_pos = node->nsubnodes;
    write_pos += SIZE_OF_CHILD_COUNT;

    // Copy the subnodes data and offsets
    for (int i = 0; i < node->nsubnodes; ++i) {
        // left to right in the children array
        // fnext could be 0 if it's leaf
        *(int64_t *)write_pos = node->children[i].fnext;
        write_pos += SIZE_OF_FILE_OFFSET;
        copy_trie_data((trie_data_t *)write_pos, &(node->children[i].data));
        write_pos += SIZE_OF_TRIE_DATA;

        // Copy the path_len
        *write_pos = node->children[i].path_len;
        write_pos += SIZE_OF_PATH_LEN;

        // Copy the path
        size_t const path_bytes = (node->children[i].path_len + 1) / 2;
        memcpy(write_pos, node->children[i].path, path_bytes);
        write_pos += path_bytes;
    }

    return write_pos;
}
