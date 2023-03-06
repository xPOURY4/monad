#include "test_util.h"
#include <gtest/gtest.h>
#include <limits.h>
#include <monad/trie/nibble.h>
#include <monad/trie/node.h>
#include <monad/trie/update.h>
#include <stdio.h>
#include <stdlib.h>

// Test Plan: constructing trie manually and test upsert

TEST(TrieMemTest, upsert_1st_key)
{
    trie_branch_node_t *root =
        (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    root->type = BRANCH;

    unsigned char *key1;
    // 1234
    key1 = (unsigned char *)calloc(32, 1);
    key1[31] = 0x34;
    key1[30] = 0x12;

    // insert 1234
    upsert(root, key1, 64, (trie_data_t *)key1);

    trie_branch_node_t *node = (trie_branch_node_t *)root->next[0];
    EXPECT_EQ(root->path_len, 0);
    EXPECT_EQ(root->nsubnodes, 1);
    EXPECT_EQ(node->path_len, 64); // root: 0x0001234
    EXPECT_EQ(get_nibble(node->path, 63), 4);
    EXPECT_EQ(get_nibble(node->path, 62), 3);
    EXPECT_EQ(node->type, LEAF);
    EXPECT_EQ(get_nibble((node->data).bytes, 63), 4);
    EXPECT_EQ(get_nibble((node->data).bytes, 62), 3);
    free(key1);
}

// test trie on inserting 1235 after 1234
TEST(TrieMemTest, upsert_2nd_key)
{
    trie_branch_node_t *root =
        (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    unsigned char *key1, *key2;
    // 1234
    key1 = (unsigned char *)calloc(32, 1);
    key1[31] = 0x34;
    key1[30] = 0x12;
    // 1235
    key2 = (unsigned char *)calloc(32, 1);
    key2[31] = 0x35;
    key2[30] = 0x12;

    // construct root node
    root->next[0] =
        (unsigned char *)get_new_leaf(key1, 64, (trie_data_t *)key1);
    root->type = BRANCH;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b01;
    // insert 1235
    upsert(root, key2, 64, (trie_data_t *)key2);
    /*
           root*
            |
          0000123
            / \
           4   5
    */
    trie_branch_node_t *node = (trie_branch_node_t *)root->next[0];
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

    free(key1);
    free(key2);
}

// test trie on inserting 1325 after 1234, 1235
// when all leaf nodes are on-disk
TEST(TrieMemTest, upsert_3rd_key_ondisk)
{
    trie_branch_node_t *root, *node;
    trie_leaf_node_t *leaf1, *leaf2;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    node = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));

    unsigned char *key1, *key2, *key3;
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

    /*   * marks in memory
            root*
             |
          0000123
            / \
           4   5
    */
    // construct root node
    root->type = BRANCH;
    root->next[0] = (unsigned char *)node;
    root->fnext[0] = ULONG_MAX;
    root->path_len = 0;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b01;

    node->type = BRANCH;
    node->path_len = 63;
    node->nsubnodes = 2;
    node->subnode_bitmask = 0b110000;
    memcpy(node->path, key1, 31);
    // construct two leaf nodes 4/5
    leaf1 = get_new_leaf(key1, 64, (trie_data_t *)key1);
    leaf2 = get_new_leaf(key2, 64, (trie_data_t *)key2);
    // make the two leaves on disk
    node->next[4] = (unsigned char *)leaf1;
    node->fnext[4] = ULONG_MAX;
    node->next[5] = (unsigned char *)leaf2;
    node->fnext[5] = ULONG_MAX;

    // insert 1325
    upsert(root, key3, 64, (trie_data_t *)key1);
    /*     root*
            |
          00001*
          / \
        23   325*
       /  \
      4    5
    */
    node = (trie_branch_node_t *)root->next[0];
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
    EXPECT_EQ(node->fnext[2], ULONG_MAX);
    EXPECT_EQ(node->fnext[3], 0);
    EXPECT_EQ(node->fnext[4], 0);
    EXPECT_EQ(node->fnext[5], 0);
    EXPECT_NE(((trie_branch_node_t *)node->next[2])->fnext[4], 0);
    EXPECT_NE(((trie_branch_node_t *)node->next[2])->fnext[5], 0);

    free(key1);
    free(key2);
    free(key3);
}

// test trie on inserting 1325 after 1234, 1235
// assume all leaf nodes are created in the same transaction, thus in ram
TEST(TrieMemTest, upsert_3rd_key_ram)
{
    trie_branch_node_t *root, *node;
    trie_leaf_node_t *leaf1, *leaf2;
    root = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));
    node = (trie_branch_node_t *)calloc(1, sizeof(trie_branch_node_t));

    unsigned char *key1, *key2, *key3;
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

    /*   * marks in memory
            root*
             |
          0000123*
            / \
           4*   5*
    */
    // construct root node
    root->type = BRANCH;
    root->next[0] = (unsigned char *)node;
    root->path_len = 0;
    root->nsubnodes = 1;
    root->subnode_bitmask = 0b01;

    node->type = BRANCH;
    node->path_len = 63;
    node->nsubnodes = 2;
    node->subnode_bitmask = 0b110000;
    memcpy(node->path, key1, 31);
    // construct two leaf nodes 4/5
    leaf1 = get_new_leaf(key1, 64, (trie_data_t *)key1);
    leaf2 = get_new_leaf(key2, 64, (trie_data_t *)key2);
    // make the two leaves on disk
    node->next[4] = (unsigned char *)leaf1;
    node->next[5] = (unsigned char *)leaf2;

    // insert 1325
    upsert(root, key3, 64, (trie_data_t *)key1);
    /*
           root*
            |
          00001*
          / \
        23*   325*
       /  \
      4*    5*
    */
    node = (trie_branch_node_t *)root->next[0];
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
    EXPECT_EQ(node->fnext[2], 0);
    EXPECT_EQ(node->fnext[3], 0);
    EXPECT_EQ(node->fnext[4], 0);
    EXPECT_EQ(node->fnext[5], 0);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->fnext[4], 0);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->fnext[5], 0);

    // commit all nodes to disk
    do_commit(node);
    EXPECT_EQ(node->fnext[2], ULONG_MAX);
    EXPECT_EQ(node->fnext[3], ULONG_MAX);
    EXPECT_EQ(node->fnext[4], 0);
    EXPECT_EQ(node->fnext[5], 0);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->fnext[4], ULONG_MAX);
    EXPECT_EQ(((trie_branch_node_t *)node->next[2])->fnext[5], ULONG_MAX);

    free(key1);
    free(key2);
    free(key3);
}