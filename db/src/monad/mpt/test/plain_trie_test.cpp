#include "gtest/gtest.h"

#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/trie.hpp>

#include <vector>

using namespace monad::mpt;
using namespace monad::literals;

Node *
upsert_vector(Compute &comp, Node *const old, std::vector<Update> update_vec)
{
    UpdateList update_ls;
    for (auto &it : update_vec) {
        update_ls.push_front(it);
    }
    return upsert(comp, old, std::move(update_ls));
}

TEST(TrieUpsert, var_length)
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0x01111111_hex, 0xdead_hex}, // 0
        {0x11111111_hex, 0xbeef_hex}, // 1
        {0x11111111aaaa_hex, 0xdeafbeef_hex}, // 2
        {0x11111111aacd_hex, 0xabcd_hex}, // 3
        {0x111a1111_hex, 0xba_hex}, // 4
        {0x111b1111_hex, 0xbabe_hex}, // 5
        {0x111b1111aaaaaaaa_hex, 0xcafe_hex}, // 6
        {0x111b1111bbbbbbbb_hex, 0xbe_hex}, // 7
    };
    EmptyCompute comp;
    Node *root;

    // insert kv 0,1,2,3
    root = upsert_vector(
        comp,
        nullptr,
        {make_update(kv[0].first, kv[0].second),
         make_update(kv[1].first, kv[1].second),
         make_update(kv[2].first, kv[2].second),
         make_update(kv[3].first, kv[3].second)});
    EXPECT_EQ(root->mask, 0b11);
    EXPECT_EQ(root->leaf_len, 0);
    EXPECT_EQ(root->hash_len, 0);
    EXPECT_EQ(root->path_bytes(), 0);
    Node *node0 = root->next(0), *node1 = root->next(1);
    EXPECT_EQ(node0->mask, 0);
    EXPECT_EQ(
        node0->path_nibble_view(), (NibblesView{1, 8, kv[0].first.data()}));
    EXPECT_EQ(node0->leaf_view(), kv[0].second);
    EXPECT_EQ(node1->mask, 1u << 0xa);
    EXPECT_EQ(
        node1->path_nibble_view(), (NibblesView{1, 8, kv[1].first.data()}));
    EXPECT_EQ(node1->leaf_view(), kv[1].second);
    Node *node1aa = node1->next(0xa);
    EXPECT_EQ(node1aa->mask, 1u << 0xa | 1u << 0xc);
    EXPECT_EQ(
        node1aa->path_nibble_view(), (NibblesView{9, 10, kv[3].first.data()}));

    EXPECT_EQ(node1aa->path_bytes(), 1);
    EXPECT_EQ(node1aa->leaf_len, 0);
    Node *node1aaaa = node1aa->next(0xa), *node1aacd = node1aa->next(0xc);
    EXPECT_EQ(node1aaaa->mask, 0);
    EXPECT_EQ(
        node1aaaa->path_nibble_view(),
        (NibblesView{11, 12, kv[2].first.data()}));
    EXPECT_EQ(node1aaaa->leaf_view(), kv[2].second);
    EXPECT_EQ(node1aacd->mask, 0);
    EXPECT_EQ(
        node1aacd->path_nibble_view(),
        (NibblesView{11, 12, kv[3].first.data()}));
    EXPECT_EQ(node1aacd->leaf_view(), kv[3].second);

    // insert kv 4,5
    root = upsert_vector(
        comp,
        root,
        {make_update(kv[4].first, kv[4].second),
         make_update(kv[5].first, kv[5].second)});
    EXPECT_EQ(root->mask, 0b11);
    node1 = root->next(1); // 1111... 111a... 111b...
    EXPECT_EQ(node1->mask, 1u << 1 | 1u << 0xa | 1u << 0xb);
    Node *node1111 = node1->next(1), *node111a = node1->next(0xa),
         *node111b = node1->next(0xb);
    EXPECT_EQ(node1111->leaf_view(), kv[1].second);
    EXPECT_EQ(
        node111a->path_nibble_view(), (NibblesView{4, 8, kv[4].first.data()}));
    EXPECT_EQ(node111a->leaf_view(), kv[4].second);
    EXPECT_EQ(node111b->leaf_view(), kv[5].second);

    // insert kv 6,7
    root = upsert_vector(
        comp,
        root,
        {make_update(kv[6].first, kv[6].second),
         make_update(kv[7].first, kv[7].second)});
    node1 = root->next(1);
    node111b = node1->next(0xb);
    EXPECT_EQ(node111b->mask, 1u << 0xa | 1u << 0xb);
    EXPECT_EQ(node111b->next(0xa)->leaf_view(), kv[6].second);
    EXPECT_EQ(
        node111b->next(0xa)->path_nibble_view(),
        (NibblesView{9, 16, kv[6].first.data()}));
    EXPECT_EQ(node111b->next(0xb)->leaf_view(), kv[7].second);
    EXPECT_EQ(
        node111b->next(0xb)->path_nibble_view(),
        (NibblesView{9, 16, kv[7].first.data()}));

    free_trie(root);
}

TEST(TrieUpsert, mismatch)
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0x12345678_hex, 0xdead_hex}, // 0
        {0x12346678_hex, 0xbeef_hex}, // 1
        {0x12445678_hex, 0xdeafbeef_hex}, // 2
        {0x12347678_hex, 0xba_hex}, // 3
        {0x123aabcd_hex, 0xbabe_hex}, // 4
    };

    EmptyCompute comp;
    Node *root;

    /* insert 12345678, 12346678, 12445678
            12
          /    \
         34      445678
        / \
    5678  6678
    */
    root = upsert_vector(
        comp,
        nullptr,
        {make_update(kv[0].first, kv[0].second),
         make_update(kv[1].first, kv[1].second),
         make_update(kv[2].first, kv[2].second)});
    EXPECT_EQ(root->mask, 0b11000);
    EXPECT_EQ(
        root->path_nibble_view(), (NibblesView{0, 2, kv[0].first.data()}));
    EXPECT_EQ(root->next(4)->leaf_view(), kv[2].second);
    Node *left_leaf = root->next(3)->next(5);
    EXPECT_EQ(left_leaf->leaf_view(), kv[0].second);
    /* insert 12347678, 123aabcd
                  12
                /    \
              3       445678
             / \
            4   aabcd
          / | \
      5678 6678 7678
    */
    root = upsert_vector(
        comp,
        root,
        {make_update(kv[3].first, kv[3].second),
         make_update(kv[4].first, kv[4].second)});
    EXPECT_EQ(root->mask, 0b11000);
    EXPECT_EQ(
        root->path_nibble_view(), (NibblesView{0, 2, kv[0].first.data()}));
    Node *node3 = root->next(3);
    EXPECT_EQ(node3->mask, 1u << 4 | 1u << 0xa);
    EXPECT_EQ(node3->hash_len, 0);
    EXPECT_EQ(node3->path_bytes(), 0);
    Node *node34 = node3->next(4);
    EXPECT_EQ(node34->mask, 0b11100000);
    EXPECT_EQ(node34->hash_len, 0);
    EXPECT_EQ(node34->path_bytes(), 0);
    EXPECT_EQ(node34->next(5)->leaf_len, 2);
    EXPECT_EQ(node34->next(5)->leaf_view(), kv[0].second);
    EXPECT_EQ(node34->next(6)->leaf_view(), kv[1].second);
    EXPECT_EQ(node34->next(7)->leaf_view(), kv[3].second);

    free_trie(root);
}