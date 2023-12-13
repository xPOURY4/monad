#include "test_fixtures_base.hpp"
#include "test_fixtures_gtest.hpp" // NOLINT

#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>
#include <monad/mpt/cache_option.hpp>
#include <monad/mpt/compute.hpp>
#include <monad/mpt/node.hpp>
#include <monad/mpt/trie.hpp>
#include <monad/mpt/update.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace monad::mpt;
using namespace monad::literals;
using namespace monad::test;

template <typename TFixture>
struct PlainTrieTest : public TFixture
{
};

using PlainTrieTypes = ::testing::Types<InMemoryTrieGTest, OnDiskTrieGTest>;
TYPED_TEST_SUITE(PlainTrieTest, PlainTrieTypes);

namespace updates
{
    std::vector<std::pair<monad::byte_string, monad::byte_string>> const kv{
        {0x01111111_hex, 0xdead_hex}, // 0
        {0x11111111_hex, 0xbeef_hex}, // 1
        {0x11111111aaaa_hex, 0xdeafbeef_hex}, // 2
        {0x11111111aacd_hex, 0xabcd_hex}, // 3
        {0x111a1111_hex, 0xba_hex}, // 4
        {0x111b1111_hex, 0xbabe_hex}, // 5
        {0x111b1111aaaaaaaa_hex, 0xcafe_hex}, // 6
        {0x111b1111bbbbbbbb_hex, 0xbe_hex}, // 7
    };
}

TYPED_TEST(PlainTrieTest, leaf_nodes_persist)
{
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(0x11_hex, monad::byte_string_view{}),
        make_update(0x1111_hex, monad::byte_string_view{}),
        make_update(0x1122_hex, monad::byte_string_view{}));
    EXPECT_EQ(this->root->mask, 0b110);

    this->root = upsert_updates(
        this->aux, this->sm, std::move(this->root), make_erase(0x1111_hex));
    EXPECT_EQ(this->root->mask, 0b100);
}

TYPED_TEST(PlainTrieTest, var_length)
{
    auto const &kv = updates::kv;
    // insert kv 0,1,2,3
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second),
        make_update(kv[2].first, kv[2].second),
        make_update(kv[3].first, kv[3].second));

    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[0].first).first->value(),
        kv[0].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first->value(),
        kv[2].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[3].first).first->value(),
        kv[3].second);

    EXPECT_EQ(this->root->mask, 0b11);
    EXPECT_EQ(this->root->value_len, 0);
    EXPECT_EQ(this->root->data_len, 0);
    EXPECT_EQ(this->root->path_bytes(), 0);
    Node *node0 = this->root->next(0);
    Node *node1 = this->root->next(1);
    EXPECT_EQ(node0->mask, 0);
    EXPECT_EQ(
        node0->path_nibble_view(), (NibblesView{1, 8, kv[0].first.data()}));
    EXPECT_EQ(node0->value(), kv[0].second);
    EXPECT_EQ(node1->mask, 1u << 0xa);
    EXPECT_EQ(
        node1->path_nibble_view(), (NibblesView{1, 8, kv[1].first.data()}));
    EXPECT_EQ(node1->value(), kv[1].second);
    Node *node1aa = node1->next(0);
    EXPECT_EQ(node1aa->mask, 1u << 0xa | 1u << 0xc);
    EXPECT_EQ(
        node1aa->path_nibble_view(), (NibblesView{9, 10, kv[3].first.data()}));

    EXPECT_EQ(node1aa->path_bytes(), 1);
    EXPECT_EQ(node1aa->value_len, 0);
    Node *node1aaaa = node1aa->next(0);
    Node *node1aacd = node1aa->next(1);
    EXPECT_EQ(node1aaaa->mask, 0);
    EXPECT_EQ(
        node1aaaa->path_nibble_view(),
        (NibblesView{11, 12, kv[2].first.data()}));
    EXPECT_EQ(node1aaaa->value(), kv[2].second);
    EXPECT_EQ(node1aacd->mask, 0);
    EXPECT_EQ(
        node1aacd->path_nibble_view(),
        (NibblesView{11, 12, kv[3].first.data()}));
    EXPECT_EQ(node1aacd->value(), kv[3].second);

    // insert kv 4,5
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[4].first, kv[4].second),
        make_update(kv[5].first, kv[5].second));
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[0].first).first->value(),
        kv[0].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first->value(),
        kv[2].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[3].first).first->value(),
        kv[3].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[4].first).first->value(),
        kv[4].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[5].first).first->value(),
        kv[5].second);

    EXPECT_EQ(this->root->mask, 0b11);
    node1 = this->root->next(1); // 1111... 111a... 111b...
    EXPECT_EQ(node1->mask, 1u << 1 | 1u << 0xa | 1u << 0xb);
    Node *node1111 = node1->next(0);
    Node *node111a = node1->next(1);
    Node *node111b = node1->next(2);
    EXPECT_EQ(node1111->value(), kv[1].second);
    EXPECT_EQ(
        node111a->path_nibble_view(), (NibblesView{4, 8, kv[4].first.data()}));
    EXPECT_EQ(node111a->value(), kv[4].second);
    EXPECT_EQ(node111b->value(), kv[5].second);

    // insert kv 6,7
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[6].first, kv[6].second),
        make_update(kv[7].first, kv[7].second));
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[5].first).first->value(),
        kv[5].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[6].first).first->value(),
        kv[6].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[7].first).first->value(),
        kv[7].second);

    node1 = this->root->next(this->root->to_child_index(1));
    node111b = node1->next(node1->to_child_index(0xb));
    EXPECT_EQ(node111b->mask, 1u << 0xa | 1u << 0xb);
    EXPECT_EQ(
        node111b->next(node111b->to_child_index(0xa))->value(), kv[6].second);
    EXPECT_EQ(
        node111b->next(node111b->to_child_index(0xa))->path_nibble_view(),
        (NibblesView{9, 16, kv[6].first.data()}));
    EXPECT_EQ(
        node111b->next(node111b->to_child_index(0xb))->value(), kv[7].second);
    EXPECT_EQ(
        node111b->next(node111b->to_child_index(0xb))->path_nibble_view(),
        (NibblesView{9, 16, kv[7].first.data()}));
}

TYPED_TEST(PlainTrieTest, mismatch)
{
    std::vector<std::pair<monad::byte_string, monad::byte_string>> const kv{
        {0x12345678_hex, 0xdead_hex}, // 0
        {0x12346678_hex, 0xbeef_hex}, // 1
        {0x12445678_hex, 0xdeafbeef_hex}, // 2
        {0x12347678_hex, 0xba_hex}, // 3
        {0x123aabcd_hex, 0xbabe_hex}, // 4
    };

    /* insert 12345678, 12346678, 12445678
            12
          /    \
         34      445678
        / \
    5678  6678
    */
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second),
        make_update(kv[2].first, kv[2].second));
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[0].first).first->value(),
        kv[0].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first->value(),
        kv[2].second);

    EXPECT_EQ(this->root->mask, 0b11000);
    EXPECT_EQ(
        this->root->path_nibble_view(),
        (NibblesView{0, 2, kv[0].first.data()}));
    EXPECT_EQ(this->root->next(1)->value(), kv[2].second);
    Node *left_leaf = this->root->next(0)->next(0);
    EXPECT_EQ(left_leaf->value(), kv[0].second);
    /* insert 12347678, 123aabcd
                  12
                /    \
              3       445678
             / \
            4   aabcd
          / | \
      5678 6678 7678
    */
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[3].first, kv[3].second),
        make_update(kv[4].first, kv[4].second));
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first->value(),
        kv[2].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[3].first).first->value(),
        kv[3].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[4].first).first->value(),
        kv[4].second);

    EXPECT_EQ(this->root->mask, 0b11000);
    EXPECT_EQ(
        this->root->path_nibble_view(),
        (NibblesView{0, 2, kv[0].first.data()}));
    Node *node3 = this->root->next(0);
    EXPECT_EQ(node3->mask, 1u << 4 | 1u << 0xa);
    EXPECT_EQ(node3->data_len, 0);
    EXPECT_EQ(node3->path_bytes(), 0);
    Node *node34 = node3->next(0);
    EXPECT_EQ(node34->mask, 0b11100000);
    EXPECT_EQ(node34->data_len, 0);
    EXPECT_EQ(node34->path_bytes(), 0);
    EXPECT_EQ(node34->next(0)->value_len, 2);
    EXPECT_EQ(node34->next(0)->value(), kv[0].second);
    EXPECT_EQ(node34->next(1)->value(), kv[1].second);
    EXPECT_EQ(node34->next(2)->value(), kv[3].second);
}

TYPED_TEST(PlainTrieTest, delete_wo_incarnation)
{
    auto const &kv = updates::kv;

    // insert all
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second),
        make_update(kv[2].first, kv[2].second),
        make_update(kv[3].first, kv[3].second),
        make_update(kv[4].first, kv[4].second),
        make_update(kv[5].first, kv[5].second),
        make_update(kv[6].first, kv[6].second),
        make_update(kv[7].first, kv[7].second));
    // erase 0
    this->root = upsert_updates(
        this->aux, this->sm, std::move(this->root), make_erase(kv[0].first));
    EXPECT_EQ(this->root->mask, 2 | 1u << 0xa | 1u << 0xb);
    EXPECT_EQ(
        this->root->path_nibble_view(),
        (NibblesView{0, 3, kv[1].first.data()}));

    // erase 5, a leaf with children (consequently 6 and 7 are erased)
    this->root = upsert_updates(
        this->aux, this->sm, std::move(this->root), make_erase(kv[5].first));
    EXPECT_EQ(this->root->mask, 2 | 1u << 0xa);
    EXPECT_EQ(
        this->root->path_nibble_view(),
        (NibblesView{0, 3, kv[1].first.data()}));

    // erase 1, consequently 2,3 are erased
    this->root = upsert_updates(
        this->aux, this->sm, std::move(this->root), make_erase(kv[1].first));
    EXPECT_EQ(this->root->mask, 0);
    EXPECT_EQ(this->root->value(), kv[4].second);
    EXPECT_EQ(
        this->root->path_nibble_view(),
        (NibblesView{0, 8, kv[4].first.data()}));
}

TYPED_TEST(PlainTrieTest, delete_with_incarnation)
{
    // upsert a bunch of var lengths kv
    auto const &kv = updates::kv;
    // insert
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[0].first, kv[0].second), // 0x01111111
        make_update(kv[1].first, kv[1].second), // 0x11111111
        make_update(kv[2].first, kv[2].second)); // 0x11111111aaaa
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[0].first).first->value(),
        kv[0].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first->value(),
        kv[2].second);

    // upsert a bunch of new kvs, with incarnation flag set
    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(kv[1].first, kv[1].second, true), // 0x11111111
        make_update(kv[3].first, kv[3].second)); // 0x11111111aacd
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[0].first).first->value(),
        kv[0].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[1].first).first->value(),
        kv[1].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[3].first).first->value(),
        kv[3].second);
    EXPECT_EQ(
        find_blocking(this->aux, this->root.get(), kv[2].first).first, nullptr);
}

TYPED_TEST(PlainTrieTest, large_values)
{
    auto const key1 = 0x12_hex;
    auto const key2 = 0x13_hex;
    auto const value1 = monad::byte_string(0x6000, 0xf);
    auto const value2 = monad::byte_string(0x6000, 0x3);

    this->root = upsert_updates(
        this->aux,
        this->sm,
        std::move(this->root),
        make_update(key1, value1),
        make_update(key2, value2));

    {
        auto [leaf, res] = find_blocking(this->aux, this->root.get(), key1);
        EXPECT_EQ(res, find_result::success);
        EXPECT_NE(leaf, nullptr);
        EXPECT_TRUE(leaf->has_value());
        EXPECT_EQ(leaf->value(), value1);
    }

    {
        auto [leaf, res] = find_blocking(this->aux, this->root.get(), key2);
        EXPECT_EQ(res, find_result::success);
        EXPECT_NE(leaf, nullptr);
        EXPECT_TRUE(leaf->has_value());
        EXPECT_EQ(leaf->value(), value2);
    }
}
