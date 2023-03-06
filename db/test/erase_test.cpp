#include "test_util.h"
#include <gtest/gtest.h>
#include <limits.h>
#include <monad/trie/nibble.h>
#include <monad/trie/node.h>
#include <monad/trie/update.h>
#include <stdio.h>
#include <stdlib.h>

// Test Plan: constructing trie manually and test erase
TEST(TrieMemTest, erase_ondisk_leaf)
{
    trie_branch_node_t *root, *branch, *node;
    trie_leaf_node_t *leaf1, *leaf2, *leaf3;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    node = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    branch = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    unsigned char *key1, *key2, *key3;
    // 1234
    key1 = (unsigned char *)calloc(1, 32);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(1, 32);
    key2[31] = 0x35;
    key2[30] = 0x12;
    // 1325
    key3 = (unsigned char *)calloc(1, 32);
    key3[31] = 0x25;
    key3[30] = 0x13;
    // construct two leaf nodes 4/5
    leaf1 = get_new_leaf(key1, 64, (trie_data_t *)key1);
    leaf2 = get_new_leaf(key2, 64, (trie_data_t *)key2);
    leaf3 = get_new_leaf(key3, 64, (trie_data_t *)key3);

    root->type = BRANCH;
    root->next[0] = (unsigned char *)node;
    root->fnext[0] = ULONG_MAX;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b001;

    // make the nodes on disk
    node->type = BRANCH;
    node->path_len = 61;
    node->nsubnodes = 2;
    node->subnode_bitmask = 0b1100;
    node->next[2] = (unsigned char *)branch;
    node->fnext[2] = ULONG_MAX;
    node->next[3] = (unsigned char *)leaf3;
    node->fnext[3] = ULONG_MAX;
    memcpy(node->path, key1, 31);

    /*   * marks in memory
              root*
                |
              00001
              / \
            23   325
           /  \
          4    5
    */

    // construct branch node 23
    branch->type = BRANCH;
    branch->path_len = 63;
    branch->nsubnodes = 2;
    branch->subnode_bitmask = 0b110000;
    branch->fnext[4] = ULONG_MAX;
    branch->fnext[5] = ULONG_MAX;
    branch->next[4] = (unsigned char *)leaf1;
    branch->next[5] = (unsigned char *)leaf2;
    memcpy(branch->path, key1, 32);

    // erase 1235
    erase(root, key2, 64);
    /*   * marks in memory
          root*
            |
          00001*
          / \
        234   325
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(count_num_leaves(root), 2);
    EXPECT_EQ(root->fnext[0], 0);
    EXPECT_EQ(node->path_len, 61);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_NE(node->next[2], nullptr);
    EXPECT_NE(node->next[3], nullptr);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->type, LEAF);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->path_len, 64);
    EXPECT_EQ(((trie_branch_node_t *)node->next[3])->path_len, 64);
    EXPECT_EQ(node->fnext[2], ULONG_MAX); // leaf234 is the original leaf 4
    EXPECT_EQ(node->fnext[3], ULONG_MAX);
    EXPECT_EQ((node->subnode_bitmask & ~0b001100), 0);

    free(key1);
    free(key2);
    free(key3);
}

TEST(TrieMemTest, erase_mem_leaf)
{
    trie_branch_node_t *root, *branch, *node;
    trie_leaf_node_t *leaf1, *leaf2, *leaf3;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    branch = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    node = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    unsigned char *key1, *key2, *key3;
    // 1234
    key1 = (unsigned char *)calloc(1, 32);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(1, 32);
    key2[31] = 0x35;
    key2[30] = 0x12;
    // 1325
    key3 = (unsigned char *)calloc(1, 32);
    key3[31] = 0x25;
    key3[30] = 0x13;

    leaf1 = get_new_leaf(key1, 64, (trie_data_t *)key1);
    leaf2 = get_new_leaf(key2, 64, (trie_data_t *)key2);
    leaf3 = get_new_leaf(key3, 64, (trie_data_t *)key3);

    /*   * marks in memory
    to simulate
    upsert(1234), upsert(1235), commit, upsert(1325)
    then do erase(1325)
              root*
                |
              00001*
              / \
            23   325*
           /  \
          4    5
    */
    // construct root node as 0..01
    root->type = BRANCH;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b01;
    root->next[0] = (unsigned char *)node; // in memory

    // construct direct subnode of root
    node->type = BRANCH;
    node->path_len = 61;
    node->nsubnodes = 2;
    node->subnode_bitmask = 0b1100;
    // make the left branch on disk
    node->next[2] = (unsigned char *)branch;
    node->next[3] = (unsigned char *)leaf3;
    node->fnext[2] = ULONG_MAX;
    node->fnext[3] = 0;
    memcpy(node->path, key1, 31);

    // construct branch node 23
    branch->type = BRANCH;
    branch->path_len = 63;
    branch->nsubnodes = 2;
    branch->subnode_bitmask = 0b110000;
    branch->fnext[4] = ULONG_MAX;
    branch->fnext[5] = ULONG_MAX;
    branch->next[4] = (unsigned char *)leaf1;
    branch->next[5] = (unsigned char *)leaf2;
    memcpy(branch->path, key1, 32);

    // erase 1325
    erase(root, key3, 64);
    /*   * marks in memory
         root*
           |
        0000123
          / \
        4    5
    */
    node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(count_num_leaves(root), 2);
    EXPECT_EQ(root->fnext[0], ULONG_MAX);
    EXPECT_EQ(node->path_len, 63);
    EXPECT_EQ(node->nsubnodes, 2);
    EXPECT_NE(node->next[4], nullptr);
    EXPECT_NE(node->next[5], nullptr);
    EXPECT_EQ(((trie_branch_node_t *)node->next[4])->path_len, 64);
    EXPECT_EQ(((trie_branch_node_t *)node->next[5])->path_len, 64);
    EXPECT_EQ(node->fnext[4], ULONG_MAX);
    EXPECT_EQ(node->fnext[5], ULONG_MAX);
    EXPECT_EQ((node->subnode_bitmask & ~0b110000), 0);

    free(key1);
    free(key2);
    free(key3);
}

TEST(TrieMemTest, erase_all_leaves)
{
    trie_branch_node_t *root, *branch, *node;
    trie_leaf_node_t *leaf1, *leaf2, *leaf3;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    branch = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    node = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    unsigned char *key1, *key2, *key3, *key4;
    // 1234
    key1 = (unsigned char *)calloc(1, 32);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(1, 32);
    key2[31] = 0x35;
    key2[30] = 0x12;
    // 1325
    key3 = (unsigned char *)calloc(1, 32);
    key3[31] = 0x25;
    key3[30] = 0x13;
    // 1236
    key4 = (unsigned char *)calloc(1, 32);
    key4[31] = 0x36;
    key4[30] = 0x12;

    leaf1 = get_new_leaf(key1, 64, (trie_data_t *)key1);
    leaf2 = get_new_leaf(key2, 64, (trie_data_t *)key2);
    leaf3 = get_new_leaf(key3, 64, (trie_data_t *)key3);

    // construct root node as 0..01
    root->type = BRANCH;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b01;
    root->next[0] = (unsigned char *)node; // in memory

    // construct direct subnode of root
    node->type = BRANCH;
    node->path_len = 61;
    node->nsubnodes = 2;
    node->subnode_bitmask = 0b1100;
    // make the left branch on disk
    node->next[2] = (unsigned char *)branch;
    node->next[3] = (unsigned char *)leaf3;
    node->fnext[2] = ULONG_MAX;
    node->fnext[3] = 0;
    memcpy(node->path, key1, 31);

    // construct branch node 23
    branch->type = BRANCH;
    branch->path_len = 63;
    branch->nsubnodes = 2;
    branch->subnode_bitmask = 0b110000;
    branch->fnext[4] = ULONG_MAX;
    branch->fnext[5] = ULONG_MAX;
    branch->next[4] = (unsigned char *)leaf1;
    branch->next[5] = (unsigned char *)leaf2;
    memcpy(branch->path, key1, 32);

    upsert(root, key4, 64, (trie_data_t *)key4);
    /*   * marks in memory
    to simulate
    upsert(1234), upsert(1235), commit, upsert(1325) upsert(1236)
    then erase all
              root*
                |
              00001*
              / \
            23   325*
          / | \
         4  5  6*
    */
    EXPECT_EQ(
        ((trie_branch_node_t *)((trie_branch_node_t *)root->next[0])->next[2])
            ->subnode_bitmask,
        0b1110000);
    // start erase
    erase(root, key4, 64);
    EXPECT_EQ(count_num_leaves(root), 3);
    erase(root, key3, 64);
    EXPECT_EQ(count_num_leaves(root), 2);
    erase(root, key1, 64);
    EXPECT_EQ(count_num_leaves(root), 1);
    erase(root, key2, 64);

    EXPECT_EQ(count_num_leaves(root), 0);
    EXPECT_EQ(root->path_len, 0);
    EXPECT_EQ(root->nsubnodes, 0);
    EXPECT_EQ(root->subnode_bitmask, 0);
}

TEST(TrieMemTest, upsert_then_erase_all)
{
    trie_branch_node_t *root;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    root->type = BRANCH;
    root->path_len = 0;
    unsigned char *key1, *key2, *key3;
    // 1234
    key1 = (unsigned char *)calloc(1, 32);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(1, 32);
    key2[31] = 0x35;
    key2[30] = 0x12;
    // 1325
    key3 = (unsigned char *)calloc(1, 32);
    key3[31] = 0x25;
    key3[30] = 0x13;

    /*   * marks in memory
    to simulate
    upsert(1234), upsert(1235), commit, upsert(1325)
    then do erase(1325)
              root*
                |
              00001*
              / \
            23*   325*
           /  \
          4*    5*
    */
    upsert(root, key1, 64, (trie_data_t *)key1);
    upsert(root, key2, 64, (trie_data_t *)key2);
    upsert(root, key3, 64, (trie_data_t *)key2);
    upsert(root, key2, 64, (trie_data_t *)key1);

    // start erase
    erase(root, key3, 64);
    erase(root, key1, 64);
    erase(root, key2, 64);

    EXPECT_EQ(count_num_leaves(root), 0);
    EXPECT_EQ(root->path_len, 0);
    EXPECT_EQ(root->nsubnodes, 0);
    EXPECT_EQ(root->subnode_bitmask, 0);
}