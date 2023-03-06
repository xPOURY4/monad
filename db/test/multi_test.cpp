#include "test_util.h"
#include <gtest/gtest.h>
#include <limits.h>
#include <monad/trie/nibble.h>
#include <monad/trie/node.h>
#include <monad/trie/update.h>
#include <stdio.h>
#include <stdlib.h>

// Test Plan: test multi-version trie structure after multiple commits
// wip

TEST(TrieMemTest, commit)
{
    trie_branch_node_t *root, *new_root, *node;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    root->type = BRANCH;
    unsigned char *key1, *key2, *key3, *key4;

    // 1234
    key1 = (unsigned char *)calloc(32, 1);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(32, 1);
    key2[31] = 0x35;
    key2[30] = 0x12;
    // 1325
    key3 = (unsigned char *)calloc(32, 1);
    key3[31] = 0x25;
    key3[30] = 0x13;
    // 1456
    key4 = (unsigned char *)calloc(32, 1);
    key4[31] = 0x56;
    key4[30] = 0x14;

    // insert 1234
    upsert(root, key1, 64, (trie_data_t *)key1);

    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(count_num_leaves(root), 1);
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(node->path_len, 64); // 0x0001234
    EXPECT_EQ(get_nibble(node->path, 63), 4);
    EXPECT_EQ(node->type, LEAF);

    upsert(root, key2, 64, (trie_data_t *)key2);
    /*
            root
             |
          0000123
            / \
           4   5
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(count_num_leaves(root), 2);
    EXPECT_EQ(node->path_len, 63);
    EXPECT_EQ(node->type, BRANCH);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_EQ(get_nibble(node->path, 60), 0x01);
    EXPECT_NE(node->next[4], nullptr);
    EXPECT_NE(node->next[5], nullptr);
    EXPECT_EQ(((trie_leaf_node_t *)node->next[4])->type, LEAF);
    EXPECT_EQ(((trie_branch_node_t *)node->next[4])->path_len, 64);
    EXPECT_EQ(((trie_branch_node_t *)node->next[5])->path_len, 64);
    EXPECT_EQ((node->subnode_bitmask & ~0b110000), 0);

    upsert(root, key3, 64, (trie_data_t *)key3);
    /*
               root
                |
              00001
              / \
            23   325
           /  \
          4    5
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(count_num_leaves(root), 3);
    EXPECT_EQ(node->path_len, 61);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_NE(node->next[2], nullptr);
    EXPECT_NE(node->next[3], nullptr);
    EXPECT_EQ(node->next[4], nullptr);
    EXPECT_EQ(node->next[5], nullptr);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->type, BRANCH);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->path_len, 63);
    EXPECT_EQ(((trie_branch_node_t *)node->next[3])->path_len, 64);
    EXPECT_EQ(
        ((trie_branch_node_t *)((trie_branch_node_t *)node->next[2])->next[5])
            ->path_len,
        64);
    EXPECT_EQ(
        ((trie_branch_node_t *)((trie_branch_node_t *)node->next[2])->next[4])
            ->path_len,
        64);
    EXPECT_EQ(((trie_leaf_node_t *)node->next[3])->type, LEAF);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->nsubnodes, 2);
    EXPECT_EQ((node->subnode_bitmask & ~0b001100), 0);
    EXPECT_EQ(
        (((trie_branch_node_t *)node->next[2])->subnode_bitmask & ~0b0110000),
        0);

    // erase key2 1235
    erase(root, key2, 64);
    /*
            root
              |
            00001
            / \
         234   325
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(count_num_leaves(root), 2);
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(node->path_len, 61);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_NE(node->next[2], nullptr);
    EXPECT_NE(node->next[3], nullptr);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->path_len, 64);
    EXPECT_EQ(((trie_branch_node_t *)node->next[3])->path_len, 64);
    EXPECT_EQ((node->subnode_bitmask & ~0b001100), 0);

    // insert 1456
    upsert(root, key4, 64, (trie_data_t *)key4);
    /*
              root
               |
             00001
            /  |  \
         234   325 456
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(count_num_leaves(root), 3);
    EXPECT_EQ((node->subnode_bitmask & ~0b011100), 0);
    EXPECT_EQ(node->path_len, 61);
    EXPECT_EQ(node->nsubnodes, 3);

    // erase key1 1234
    erase(root, key1, 64);
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(count_num_leaves(root), 2);
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(node->path_len, 61);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_EQ((node->subnode_bitmask & ~0b011000), 0);

    /*
             root
              |
            00001
            /   \
         325      456
    */
    do_commit(root);

    new_root = copy_node(root); // starts in memory
    // insert 120......2345
    key1[0] = 0x12;
    key1[30] = 0x23;
    key1[31] = 0x45;
    upsert(new_root, key1, 64, (trie_data_t *)key1);
    /*
                 root*
                /     \
            00001      120...02345*
            /   \
         325      456
    */
    EXPECT_EQ(new_root->nsubnodes, 2);
    EXPECT_EQ(new_root->path_len, 0);
    EXPECT_NE(new_root->next[0], nullptr);
    EXPECT_NE(new_root->next[1], nullptr);
    EXPECT_EQ(new_root->fnext[0], ULONG_MAX);
    EXPECT_EQ(new_root->fnext[1], 0); // in memory
    EXPECT_EQ((new_root->subnode_bitmask & ~0b011), 0);
    EXPECT_EQ(((trie_branch_node_t *)new_root->next[0])->path_len, 61);
    EXPECT_EQ(((trie_branch_node_t *)new_root->next[1])->path_len, 64);

    // erase 0x0..1456
    erase(new_root, key4, 64);
    /*
                 root*
                /     \
            00001325   120...02345*

    */
    EXPECT_EQ(new_root->path_len, 0);
    EXPECT_NE(new_root->next[0], nullptr);
    EXPECT_NE(new_root->next[1], nullptr);
    EXPECT_EQ(new_root->fnext[0], ULONG_MAX);
    EXPECT_EQ(new_root->fnext[1], 0); // in memory
    EXPECT_EQ((new_root->subnode_bitmask & ~0b011), 0);
    EXPECT_EQ(((trie_branch_node_t *)new_root->next[0])->path_len, 64);
    EXPECT_EQ(((trie_branch_node_t *)new_root->next[1])->path_len, 64);

    free(key1);
    free(key2);
    free(key3);
}
