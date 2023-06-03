#include <monad/trie/nibble.hpp>
#include <monad/trie/tmp_trie.hpp>
#include <string.h>

#include <cstring>

MONAD_TRIE_NAMESPACE_BEGIN

uint32_t TmpTrie::get_new_branch(
    unsigned char const *const path, unsigned char const path_len)
{
    uint32_t branch_i = cpool_reserve29(tmppool_, sizeof(tmp_branch_node_t));
    cpool_advance29(tmppool_, sizeof(tmp_branch_node_t));
    // allocate the next spot for branch
    tmp_branch_node_t *branch =
        reinterpret_cast<tmp_branch_node_t *>(cpool_ptr29(tmppool_, branch_i));
    std::memset(branch, 0, sizeof(tmp_branch_node_t));
    branch->type = tmp_node_type_t::BRANCH;
    branch->path_len = path_len;
    std::memcpy(branch->path, path, (path_len + 1) / 2);
    return branch_i;
}

uint32_t TmpTrie::get_new_leaf(
    unsigned char const *const path, unsigned char const path_len,
    trie_data_t const *const data, bool tombstone)
{
    uint32_t leaf_i = cpool_reserve29(tmppool_, sizeof(tmp_leaf_node_t));
    cpool_advance29(tmppool_, sizeof(tmp_leaf_node_t));
    tmp_leaf_node_t *leaf =
        reinterpret_cast<tmp_leaf_node_t *>(cpool_ptr29(tmppool_, leaf_i));
    std::memset(leaf, 0, sizeof(tmp_leaf_node_t));
    leaf->type = tmp_node_type_t::LEAF;
    leaf->path_len = path_len;
    std::memcpy(leaf->path, path, (path_len + 1) / 2);
    std::memcpy(&leaf->data, data, 32);
    leaf->tombstone = tombstone;
    return leaf_i;
}

void TmpTrie::upsert(
    unsigned char const *const path, uint8_t const path_len,
    trie_data_t const *const data, bool const erase)
{ // TODO: trie data pass in as reference, use byte_string_view to include
  // length info
    int key_index = 0;
    unsigned char path_nibble;
    unsigned char node_nibble;

    uint32_t node_i = root_i_;
    unsigned char nibble = 0;
    tmp_branch_node_t *node = get_node(node_i), *parent_node = NULL;

    while (key_index < path_len) {
        path_nibble = get_nibble(path, key_index);

        if (key_index >= node->path_len) {
            // Case 1: Reached the end of path in node
            // check if there's an edge with path_nibble
            // to another node
            if (node->subnode_bitmask & 1u << path_nibble) {
                // Case 1.1: There's a subnode to traverse further
                // Update the node as child to keep traversing
                parent_node = node;
                nibble = path_nibble;
                node_i = parent_node->next[path_nibble];
                node = get_node(node_i);

                continue;
                key_index++;
            }
            else {
                // Case 1.2: There's no edge
                // add a new branch at current node
                node->next[path_nibble] =
                    get_new_leaf(path, path_len, data, erase);
                ++node->nsubnodes;
                node->subnode_bitmask |= 1u << path_nibble;
                return;
            }
        }

        node_nibble = get_nibble(node->path, key_index);
        if (node_nibble != path_nibble) {
            // Case 2: Nibbles are not equal
            // create a new branch that branches out at node_i, update parent
            uint32_t new_branch_i = get_new_branch(path, key_index);
            parent_node->next[nibble] = new_branch_i;
            tmp_branch_node_t *new_branch = get_node(new_branch_i);
            new_branch->next[path_nibble] =
                get_new_leaf(path, path_len, data, erase);
            new_branch->next[node_nibble] = node_i;
            new_branch->nsubnodes = 2;
            new_branch->subnode_bitmask |=
                (1u << path_nibble) | (1u << node_nibble);
            return;
        }
        // node_nibble == path_nibble. keep traversing common prefix
        key_index++;
    }
    return;
}

MONAD_TRIE_NAMESPACE_END