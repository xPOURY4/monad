#include "test_fixtures_base.hpp"

#include <monad/mpt/db.hpp>
#include <monad/mpt/db_options.hpp>

#include <gtest/gtest.h>

using namespace monad::mpt;
using namespace monad::test;

namespace
{
    struct InMemoryDbFixture : public ::testing::Test
    {
        StateMachineAlwaysMerkle machine{};
        Db db{DbOptions{machine, false}};
    };

    struct OnDiskDbFixture : public ::testing::Test
    {
        StateMachineAlwaysMerkle machine{};
        Db db{DbOptions{machine, true}};
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
