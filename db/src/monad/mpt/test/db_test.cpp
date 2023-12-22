#include "test_fixtures_base.hpp"

#include <monad/mpt/db.hpp>
#include <monad/mpt/db_options.hpp>
#include <monad/mpt/traverse.hpp>

#include <gtest/gtest.h>

using namespace monad::mpt;
using namespace monad::test;

namespace
{
    struct InMemoryDbFixture : public ::testing::Test
    {
        StateMachineAlwaysMerkle machine{};
        Db db{machine, DbOptions{.on_disk = false}};
    };

    struct OnDiskDbFixture : public ::testing::Test
    {
        StateMachineAlwaysMerkle machine{};
        Db db{machine, DbOptions{.on_disk = true}};
    };
}

template <typename TFixture>
struct DbTest : public TFixture
{
};

using DbTypes = ::testing::Types<InMemoryDbFixture, OnDiskDbFixture>;
TYPED_TEST_SUITE(DbTest, DbTypes);

TYPED_TEST(DbTest, simple)
{
    auto const &kv = fixed_updates::kv;

    auto const prefix = 0x00_hex;

    {
        auto u1 = make_update(kv[0].first, kv[0].second);
        auto u2 = make_update(kv[1].first, kv[1].second);
        UpdateList ul;
        ul.push_front(u1);
        ul.push_front(u2);

        auto u_prefix = Update{
            .key = prefix,
            .value = monad::byte_string_view{},
            .incarnation = false,
            .next = std::move(ul)};
        UpdateList ul_prefix;
        ul_prefix.push_front(u_prefix);

        this->db.upsert(std::move(ul_prefix));
    }

    EXPECT_EQ(this->db.get(prefix + kv[0].first).value(), kv[0].second);
    EXPECT_EQ(this->db.get(prefix + kv[1].first).value(), kv[1].second);
    EXPECT_EQ(
        this->db.get_data(prefix).value(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);

    {
        auto u1 = make_update(kv[2].first, kv[2].second);
        auto u2 = make_update(kv[3].first, kv[3].second);
        UpdateList ul;
        ul.push_front(u1);
        ul.push_front(u2);

        auto u_prefix = Update{
            .key = prefix,
            .value = monad::byte_string_view{},
            .incarnation = false,
            .next = std::move(ul)};
        UpdateList ul_prefix;
        ul_prefix.push_front(u_prefix);

        this->db.upsert(std::move(ul_prefix));
    }

    EXPECT_EQ(this->db.get(prefix + kv[2].first).value(), kv[2].second);
    EXPECT_EQ(this->db.get(prefix + kv[3].first).value(), kv[3].second);
    EXPECT_EQ(
        this->db.get_data(prefix).value(),
        0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex);

    EXPECT_FALSE(this->db.get(0x01_hex).has_value());
}

TYPED_TEST(DbTest, traverse)
{
    auto const k1 = 0x12345678_hex;
    auto const v1 = 0xcafebabe_hex;
    auto const k2 = 0x12346678_hex;
    auto const v2 = 0xdeadbeef_hex;
    auto const k3 = 0x12445678_hex;
    auto const v3 = 0xdeadbabe_hex;
    auto u1 = make_update(k1, v1);
    auto u2 = make_update(k2, v2);
    auto u3 = make_update(k3, v3);
    UpdateList ul;
    ul.push_front(u1);
    ul.push_front(u2);
    ul.push_front(u3);

    auto const prefix = 0x00_hex;
    auto u_prefix = Update{
        .key = prefix,
        .value = monad::byte_string_view{},
        .incarnation = false,
        .next = std::move(ul)};

    UpdateList ul_prefix;
    ul_prefix.push_front(u_prefix);
    this->db.upsert(std::move(ul_prefix));

    /*
            00
            |
            12
          /    \
         34      445678
        / \
     5678  6678
    */
    struct SimpleTraverse : public TraverseMachine
    {
        size_t index = 0;
        size_t num_up = 0;

        virtual void down(unsigned char const branch, Node const &node) override
        {
            if (index == 0) {
                EXPECT_EQ(branch, INVALID_BRANCH);
                EXPECT_EQ(node.number_of_children(), 1);
                EXPECT_EQ(node.mask, 0b10);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), monad::byte_string_view{});
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(node.path_nibble_view(), NibblesView(0x00_hex));
            }
            else if (index == 1) {
                EXPECT_EQ(branch, 1);
                EXPECT_EQ(node.number_of_children(), 2);
                EXPECT_EQ(node.mask, 0b11000);
                EXPECT_FALSE(node.has_value());
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(node.path_nibble_view(), make_nibbles({0x2}));
            }
            else if (index == 2) {
                EXPECT_EQ(branch, 3);
                EXPECT_EQ(node.number_of_children(), 2);
                EXPECT_EQ(node.mask, 0b1100000);
                EXPECT_FALSE(node.has_value());
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(node.path_nibble_view(), make_nibbles({0x4}));
            }
            else if (index == 3) {
                EXPECT_EQ(branch, 5);
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xcafebabe_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(), make_nibbles({0x6, 0x7, 0x8}));
            }
            else if (index == 4) {
                EXPECT_EQ(branch, 6);
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xdeadbeef_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(), make_nibbles({0x6, 0x7, 0x8}));
            }
            else if (index == 5) {
                EXPECT_EQ(branch, 4);
                EXPECT_EQ(node.number_of_children(), 0);
                EXPECT_EQ(node.mask, 0);
                EXPECT_TRUE(node.has_value());
                EXPECT_EQ(node.value(), 0xdeadbabe_hex);
                EXPECT_TRUE(node.has_path());
                EXPECT_EQ(
                    node.path_nibble_view(),
                    make_nibbles({0x4, 0x5, 0x6, 0x7, 0x8}));
            }
            else {
                FAIL();
            }

            ++index;
        }

        virtual void up(unsigned char const, Node const &) override
        {
            ++num_up;
        }

        Nibbles make_nibbles(std::initializer_list<uint8_t> nibbles)
        {
            Nibbles ret{nibbles.size()};
            for (auto it = nibbles.begin(); it < nibbles.end(); ++it) {
                MONAD_ASSERT(*it <= 0xf);
                ret.set(
                    static_cast<unsigned>(std::distance(nibbles.begin(), it)),
                    *it);
            }
            return ret;
        }
    } traverse;

    this->db.traverse(prefix, traverse);
    EXPECT_EQ(traverse.index, 6);
    EXPECT_EQ(traverse.num_up, 6);
}
