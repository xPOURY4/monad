#include <monad/mpt/update.hpp>
#include <monad/trie/trie.hpp>

#include <monad/core/hex_literal.hpp>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include "trie_fixtures.hpp"
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace monad::trie;
using namespace monad::mpt;
using namespace monad::literals;

// overload
[[nodiscard]] Update
make_update(std::pair<monad::byte_string, monad::byte_string> const &kvpair)
{
    auto &[key, value] = kvpair;
    return Update{
        {key.data(), value.size() ? std::optional<Data>{value} : std::nullopt},
        UpdateMemberHook{}};
}

namespace fixed_updates
{
    // single update
    std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
};

template <typename TFixture>
struct TrieTest : public TFixture
{
};

using TrieTypes =
    ::testing::Types<in_memory_trie_fixture_t, on_disk_trie_fixture_t>;

TYPED_TEST_SUITE(TrieTest, TrieTypes);

template <typename TBase>
class TrieUpdateFixture : public TBase
{
public:
    using TBase::process_updates;

    TrieUpdateFixture()
    {
        std::vector<Update> update_vec;
        std::ranges::transform(
            fixed_updates::kv,
            std::back_inserter(update_vec),
            [](auto &su) -> Update {
                auto &[k, v] = su;
                return make_update(k, v);
            });
        process_updates({update_vec});
    }
};

using TrieUpdateTypes = ::testing::Types<
    TrieUpdateFixture<in_memory_trie_fixture_t>,
    TrieUpdateFixture<on_disk_trie_fixture_t>>;

template <typename TFixture>
struct TrieUpdateTest : public TFixture
{
};

TYPED_TEST_SUITE(TrieUpdateTest, TrieUpdateTypes);

// start typed test
TYPED_TEST(TrieTest, EmptyTrie)
{

    this->trie.set_root(get_new_merkle_node());

    EXPECT_EQ(
        this->root_hash(),
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}

TYPED_TEST(TrieTest, OneElement)
{

    // single update
    UpdateList updates;
    auto
        key =
            0x1234567812345678123456781234567812345678123456781234567812345678_hex,
        value =
            0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex;

    Update a = make_update(key, value);
    updates.push_front(a);

    this->process_updates(updates);

    EXPECT_EQ(
        this->root_hash(),
        0xa1aa368afa323866e03c21927db548afda3da793f4d3c646d7dd8109477b907e_hex);

    // update again
    value =
        0xdeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddead_hex;

    a = make_update(key, value);
    updates.pop_front();
    MONAD_ASSERT(updates.empty());
    updates.push_front(a);

    this->process_updates(updates);

    EXPECT_EQ(
        this->root_hash(),
        0x5d225e3b0f1f386171899d343211850f102fa15de6e808c6f614915333a4f3ab_hex);
}

TYPED_TEST(TrieTest, Simple)
{

    std::vector<Update> update_vec;
    update_vec.push_back(make_update(fixed_updates::kv[0]));
    update_vec.push_back(make_update(fixed_updates::kv[1]));

    this->process_updates(update_vec);

    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);

    // two other updates for next batch
    update_vec[0] = make_update(fixed_updates::kv[2]);
    update_vec[1] = make_update(fixed_updates::kv[3]);

    this->process_updates(update_vec);

    EXPECT_EQ(
        this->root_hash(),
        0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex);
}

TYPED_TEST(TrieTest, UnrelatedLeaves)
{
    std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};

    UpdateList updates;
    std::vector<Update> update_vec;
    update_vec.push_back(make_update(kv[0]));
    update_vec.push_back(make_update(kv[1]));

    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xc2cbdf038f464a595ac12a257d48cc2a36614f0adfd2e9a08b79c5b34b52316a_hex);

    // two other updates for next batch
    update_vec[0] = make_update(kv[2]);
    update_vec[1] = make_update(kv[3]);

    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xd339cf4033aca65996859d35da4612b642664cc40734dbdd40738aa47f1e3e44_hex);
}

TYPED_TEST(TrieUpdateTest, None)
{
    EXPECT_EQ(
        this->root_hash(),
        monad::byte_string(
            {0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex}));
}

TYPED_TEST(TrieUpdateTest, RemoveEverything)
{
    std::vector<Update> update_vec;
    std::ranges::transform(
        fixed_updates::kv,
        std::back_inserter(update_vec),
        [](auto &su) -> Update {
            auto &[k, v] = su;
            return make_update(k, {});
        });
    this->process_updates({update_vec});

    EXPECT_TRUE(
        this->root_hash() ==
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}

TYPED_TEST(TrieUpdateTest, DeleteSingleBranch)
{
    std::vector<Update> update_vec;
    update_vec.push_back(make_update(fixed_updates::kv[2].first, {}));
    update_vec.push_back(make_update(fixed_updates::kv[3].first, {}));

    this->process_updates({update_vec});

    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);
}

TYPED_TEST(TrieUpdateTest, DeleteOneAtATime)
{
    std::vector<Update> update_vec;

    update_vec.push_back(make_update(fixed_updates::kv[2].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xd8b34a85db25148b1901459eac9805edadaa20b03f41fecd3b571f3b549e2774_hex);

    update_vec.clear();
    update_vec.push_back(make_update(fixed_updates::kv[1].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x107c8dd7bf9e7ca1faaa2c5856b412a8d7fccfa0005ca2500673a86b9c1760de_hex);

    update_vec.clear();
    update_vec.push_back(make_update(fixed_updates::kv[0].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x15fa9c02a40994d2d4f9c9b21daba3c4e455985490de5f9ae4889548f34d5873_hex);

    update_vec.clear();
    update_vec.push_back(make_update(fixed_updates::kv[3].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}
