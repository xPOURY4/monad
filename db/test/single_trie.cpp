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
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> account_kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0x0000000000000000deadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};

    const std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
};

namespace unrelated_leaves
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> account_kv{
        {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};

    const std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex},
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabedeadbeefcafebabedeadbeefcafebabedeadbeefcafebabe_hex},
        {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafedeadcafe_hex},
        {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabedeadbabe_hex}};
};

namespace var_len_updates
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> account_kv{
        {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000dead_hex},
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000beef_hex},
        {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000ba_hex},
        {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0x0000000000000000deadcafedeadcafedeadcafedeadcafedead_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbabedeadbabedeadbabedead_hex}};

    const std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0x0234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdead_hex},
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xbeef_hex},
        {0x2234567812345678123456781234567812345678123456781234567812345678_hex,
         0xba_hex},
        {0x3234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0xdeadcafedeadcafedeadcafedeadcafedead_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbabedeadbabedeadbabedead_hex}};
};

constexpr auto &get_fixed_updates(bool is_account)
{
    return is_account ? fixed_updates::account_kv : fixed_updates::storage_kv;
}

constexpr auto &get_unrelated_updates(bool is_account)
{
    return is_account ? unrelated_leaves::account_kv
                      : unrelated_leaves::storage_kv;
}

constexpr auto &get_varlen_updates(bool is_account)
{
    return is_account ? var_len_updates::account_kv
                      : var_len_updates::storage_kv;
}

template <typename TFixture>
struct TrieTest : public TFixture
{
};

using TrieTypes = ::testing::Types<
    in_memory_trie_fixture_t<true>, on_disk_trie_fixture_t<true>,
    in_memory_trie_fixture_t<false>, on_disk_trie_fixture_t<false>>;

TYPED_TEST_SUITE(TrieTest, TrieTypes);

template <typename TBase>
class TrieUpdateFixture : public TBase
{
public:
    using TBase::process_updates;

    TrieUpdateFixture()
    {
        auto &kv = get_fixed_updates(this->is_account());

        std::vector<Update> update_vec;
        std::ranges::transform(
            kv, std::back_inserter(update_vec), [](auto &su) -> Update {
                auto &[k, v] = su;
                return make_update(k, v);
            });
        process_updates(update_vec);
    }
};

using TrieUpdateTypes = ::testing::Types<
    TrieUpdateFixture<in_memory_trie_fixture_t<true>>,
    TrieUpdateFixture<on_disk_trie_fixture_t<true>>,
    TrieUpdateFixture<in_memory_trie_fixture_t<false>>,
    TrieUpdateFixture<on_disk_trie_fixture_t<false>>>;

template <typename TFixture>
struct TrieUpdateTest : public TFixture
{
};

TYPED_TEST_SUITE(TrieUpdateTest, TrieUpdateTypes);

////////////////////////////////////////////////////////////////////
// Test Trie Updates
////////////////////////////////////////////////////////////////////

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
            this->is_account()
                ? 0x0000000000000000deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex
                : 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex;

    Update a = make_update(key, value);
    updates.push_front(a);

    this->process_updates(updates);

    EXPECT_EQ(
        this->root_hash(),
        0xa1aa368afa323866e03c21927db548afda3da793f4d3c646d7dd8109477b907e_hex);

    // update again
    value =
        this->is_account()
            ? 0x0000000000000000deaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddead_hex
            : 0xdeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddead_hex;

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
    auto &kv = get_fixed_updates(this->is_account());

    std::vector<Update> update_vec;
    update_vec.push_back(make_update(kv[0]));
    update_vec.push_back(make_update(kv[1]));

    this->process_updates(update_vec);

    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);

    // two other updates for next batch
    update_vec[0] = make_update(kv[2]);
    update_vec[1] = make_update(kv[3]);

    this->process_updates(update_vec);

    EXPECT_EQ(
        this->root_hash(),
        0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex);
}

TYPED_TEST(TrieTest, UnrelatedLeavesWithRead)
{
    UpdateList updates;
    std::vector<Update> update_vec;

    auto &kv = get_unrelated_updates(this->is_account());

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

    // read from trie
    EXPECT_EQ(this->trie.read(kv[0].first).value(), kv[0].second);

    EXPECT_EQ(this->trie.read(kv[1].first).value(), kv[1].second);

    EXPECT_EQ(this->trie.read(kv[2].first).value(), kv[2].second);

    EXPECT_EQ(this->trie.read(kv[3].first).value(), kv[3].second);
}

TYPED_TEST(TrieTest, VarLengthLeafData)
{
    UpdateList updates;
    std::vector<Update> update_vec;

    auto &kv = get_varlen_updates(this->is_account());

    update_vec.push_back(make_update(kv[0]));
    update_vec.push_back(make_update(kv[1]));

    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xb28f388f1d98e9f2fc9daa80988cb324e0d517a86fb1f46b0bf8670728143001_hex);

    // two other updates for next batch
    update_vec[0] = make_update(kv[2]);
    update_vec[1] = make_update(kv[3]);

    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x30175d933b55cc3528abc7083210296967ea3ccb2afeb12d966a7789e8d0fc1f_hex);

    // four other updates
    update_vec.clear();
    update_vec.push_back(make_update(kv[4]));
    update_vec.push_back(make_update(kv[5]));
    update_vec.push_back(make_update(kv[6]));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x399580bb7585999a086e9bc6f29af647019826b49ef9d84004b0b03323ddb212_hex);

    // read from trie
    EXPECT_EQ(this->trie.read(kv[0].first).value(), kv[0].second);

    EXPECT_EQ(this->trie.read(kv[1].first).value(), kv[1].second);

    EXPECT_EQ(this->trie.read(kv[2].first).value(), kv[2].second);

    EXPECT_EQ(this->trie.read(kv[3].first).value(), kv[3].second);

    EXPECT_EQ(this->trie.read(kv[4].first).value(), kv[4].second);

    EXPECT_EQ(this->trie.read(kv[5].first).value(), kv[5].second);

    EXPECT_EQ(this->trie.read(kv[6].first).value(), kv[6].second);

    // a bunch of erases
    update_vec.clear();
    update_vec.push_back(make_update(kv[4].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x3467f96b8c7a1f9646cbee98500111b37d160ec0f02844b2bdcb89c8bcd3878a_hex);

    update_vec.clear();
    update_vec.push_back(make_update(kv[6].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xdba3fae4737cde5014f6200508d7659ccc146b760e3a2ded47d7c422372b6b6c_hex);

    update_vec.clear();
    update_vec.push_back(make_update(kv[2].first, {}));
    update_vec.push_back(make_update(kv[3].first, {}));
    update_vec.push_back(make_update(kv[5].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xb28f388f1d98e9f2fc9daa80988cb324e0d517a86fb1f46b0bf8670728143001_hex);
}

TYPED_TEST(TrieTest, VarLengthLeafSecond)
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> account_kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000deadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0x0000000000000000deadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0x0000000000000000dead_hex}};

    const std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0xdeadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0xdead_hex}};

    auto &kv = this->is_account() ? account_kv : storage_kv;

    UpdateList updates;
    std::vector<Update> update_vec;

    std::ranges::transform(
        kv, std::back_inserter(update_vec), [](auto &su) -> Update {
            auto &[k, v] = su;
            return make_update(k, v);
        });

    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xb796133251968233b84f3fcf8af88cdb42eeabe793f27835c10e8b46c91dfa4a_hex);
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
    auto &kv = get_fixed_updates(this->is_account());

    std::vector<Update> update_vec;
    std::ranges::transform(
        kv, std::back_inserter(update_vec), [](auto &su) -> Update {
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
    auto &kv = get_fixed_updates(this->is_account());

    std::vector<Update> update_vec;
    update_vec.push_back(make_update(kv[2].first, {}));
    update_vec.push_back(make_update(kv[3].first, {}));

    this->process_updates({update_vec});

    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);
}

TYPED_TEST(TrieUpdateTest, DeleteOneAtATime)
{
    auto &kv = get_fixed_updates(this->is_account());

    std::vector<Update> update_vec;

    update_vec.push_back(make_update(kv[2].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0xd8b34a85db25148b1901459eac9805edadaa20b03f41fecd3b571f3b549e2774_hex);

    update_vec.clear();
    update_vec.push_back(make_update(kv[1].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x107c8dd7bf9e7ca1faaa2c5856b412a8d7fccfa0005ca2500673a86b9c1760de_hex);

    update_vec.clear();
    update_vec.push_back(make_update(kv[0].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x15fa9c02a40994d2d4f9c9b21daba3c4e455985490de5f9ae4889548f34d5873_hex);

    update_vec.clear();
    update_vec.push_back(make_update(kv[3].first, {}));
    this->process_updates(update_vec);
    EXPECT_EQ(
        this->root_hash(),
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}

////////////////////////////////////////////////////////////////////
// Test Read
////////////////////////////////////////////////////////////////////

TYPED_TEST(TrieUpdateTest, ReadFromTrie)
{
    auto &kv = get_fixed_updates(this->is_account());

    EXPECT_EQ(this->trie.read(kv[0].first).value(), kv[0].second);

    EXPECT_EQ(this->trie.read(kv[1].first).value(), kv[1].second);

    EXPECT_EQ(this->trie.read(kv[2].first).value(), kv[2].second);

    EXPECT_EQ(this->trie.read(kv[3].first).value(), kv[3].second);

    EXPECT_FALSE(
        this->trie
            .read(
                0x0000000000000000000000000000000000000000000000000000000000000000_hex)
            .has_value());
}

using OnDiskTrieUpdateTypes = ::testing::Types<
    TrieUpdateFixture<on_disk_trie_fixture_t<true>>,
    TrieUpdateFixture<on_disk_trie_fixture_t<false>>>;

template <typename TFixture>
struct OnDiskTrieUpdateTest : public TFixture
{
};

TYPED_TEST_SUITE(OnDiskTrieUpdateTest, OnDiskTrieUpdateTypes);

TYPED_TEST(OnDiskTrieUpdateTest, HistoryReadFromTrie)
{
    auto &fixed_kv = get_fixed_updates(this->is_account());
    auto &unrelated_kv = get_unrelated_updates(this->is_account());

    EXPECT_EQ(
        this->trie.read_history(fixed_kv[0].first, 0).value(),
        fixed_kv[0].second);

    EXPECT_EQ(
        this->trie.read_history(fixed_kv[2].first, 0).value(),
        fixed_kv[2].second);

    UpdateList updates;
    std::vector<Update> update_vec;

    // Block 1
    update_vec.push_back(make_update(unrelated_kv[0]));
    update_vec.push_back(make_update(unrelated_kv[1]));
    this->process_updates(update_vec, 1);
    EXPECT_EQ(
        this->root_hash(),
        0xd27207a40822c2595b9c0a8290ffbbe8596f5ec7b437669f929cd725a2511540_hex);

    // Block 2
    update_vec.clear();
    update_vec.push_back(make_update(unrelated_kv[2]));
    update_vec.push_back(make_update(unrelated_kv[3]));
    this->process_updates(update_vec, 2);

    // Read history
    // read kv[0] from block 1: success
    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[0].first, 1).value(),
        unrelated_kv[0].second);

    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[1].first, 1).value(),
        unrelated_kv[1].second);

    // read kv[0] from block 0: fail
    EXPECT_FALSE(this->trie.read_history(unrelated_kv[0].first, 0).has_value());

    EXPECT_EQ(
        this->root_hash(),
        0x56173e9e85728950a7eabc45bd7cf426d9d7e03c64b2b5d746575b2c10193cb1_hex);

    // key inserted at block 0 and updated at block 1
    EXPECT_EQ(
        this->trie.read_history(fixed_kv[0].first, 0).value(),
        fixed_kv[0].second);

    EXPECT_EQ(
        this->trie.read_history(fixed_kv[0].first, 1).value(),
        unrelated_kv[1].second);

    // key inserted at block 0
    EXPECT_EQ(
        this->trie.read_history(fixed_kv[1].first, 2).value(),
        fixed_kv[1].second);

    EXPECT_EQ(
        this->trie.read_history(fixed_kv[2].first, 2).value(),
        fixed_kv[2].second);

    EXPECT_EQ(
        this->trie.read_history(fixed_kv[3].first, 2).value(),
        fixed_kv[3].second);

    // key inserted at block 1
    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[0].first, 2).value(),
        unrelated_kv[0].second);

    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[1].first, 2).value(),
        unrelated_kv[1].second);

    // key inserted at block 2
    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[2].first, 2).value(),
        unrelated_kv[2].second);

    EXPECT_EQ(
        this->trie.read_history(unrelated_kv[3].first, 2).value(),
        unrelated_kv[3].second);
}