#include "gtest/gtest.h"

#include <monad/core/byte_string.hpp>
#include <monad/core/hex_literal.hpp>

#include "test_fixtures.hpp"

using namespace ::monad::test;

template <typename TFixture>
struct TrieTest : public TFixture
{
};

using TrieTypes = ::testing::Types<InMemoryTrie, OnDiskTrie>;
TYPED_TEST_SUITE(TrieTest, TrieTypes);

template <typename TFixture>
struct EraseTrieTest : public TFixture
{
};

using EraseTrieType =
    ::testing::Types<EraseFixture<InMemoryTrie>, EraseFixture<OnDiskTrie>>;
TYPED_TEST_SUITE(EraseTrieTest, EraseTrieType);

// Test Starts
TYPED_TEST(TrieTest, insert_one_element)
{
    // keys are the same
    auto const
        key =
            0x1234567812345678123456781234567812345678123456781234567812345678_hex,
        val1 =
            0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef_hex,
        val2 =
            0xdeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddeaddead_hex;

    // single update
    this->root =
        upsert_updates(this->update_aux, nullptr, make_update(key, val1));
    EXPECT_EQ(
        this->root_hash(),
        0xa1aa368afa323866e03c21927db548afda3da793f4d3c646d7dd8109477b907e_hex);

    // update again
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_update(key, val2));
    EXPECT_EQ(
        this->root_hash(),
        0x5d225e3b0f1f386171899d343211850f102fa15de6e808c6f614915333a4f3ab_hex);
}

TYPED_TEST(TrieTest, simple_inserts)
{
    auto &kv = fixed_updates::kv;

    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second));
    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);

    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[2].first, kv[2].second),
        make_update(kv[3].first, kv[3].second));
    EXPECT_EQ(
        this->root_hash(),
        0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex);
}

TYPED_TEST(TrieTest, upsert_fixed_key_length)
{
    auto &kv = var_len_updates::kv;
    // insert kv 0,1
    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second));
    EXPECT_EQ(
        this->root_hash(),
        0xb28f388f1d98e9f2fc9daa80988cb324e0d517a86fb1f46b0bf8670728143001_hex);

    // insert kv 2,3
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[2].first, kv[2].second),
        make_update(kv[3].first, kv[3].second));
    EXPECT_EQ(
        this->root_hash(),
        0x30175d933b55cc3528abc7083210296967ea3ccb2afeb12d966a7789e8d0fc1f_hex);

    // insert kv 4,5,6
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[4].first, kv[4].second),
        make_update(kv[5].first, kv[5].second),
        make_update(kv[6].first, kv[6].second));
    EXPECT_EQ(
        this->root_hash(),
        0x399580bb7585999a086e9bc6f29af647019826b49ef9d84004b0b03323ddb212_hex);

    // erases
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[4].first));
    EXPECT_EQ(
        this->root_hash(),
        0x3467f96b8c7a1f9646cbee98500111b37d160ec0f02844b2bdcb89c8bcd3878a_hex);

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[6].first));
    EXPECT_EQ(
        this->root_hash(),
        0xdba3fae4737cde5014f6200508d7659ccc146b760e3a2ded47d7c422372b6b6c_hex);

    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_erase(kv[2].first),
        make_erase(kv[3].first),
        make_erase(kv[5].first));
    EXPECT_EQ(
        this->root_hash(),
        0xb28f388f1d98e9f2fc9daa80988cb324e0d517a86fb1f46b0bf8670728143001_hex);

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[1].first));
    EXPECT_EQ(
        this->root_hash(),
        0x065ed1753a679bbde2ce3ba5af420292b86acd3fdc2ad74215d54cc10b2add72_hex);

    // erase the last one
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[0].first));
    EXPECT_EQ(this->root.get(), nullptr);
}

TYPED_TEST(TrieTest, insert_unrelated_leaves_then_read)
{
    auto &kv = unrelated_leaves::kv;

    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second));
    EXPECT_EQ(
        this->root_hash(),
        0xc2cbdf038f464a595ac12a257d48cc2a36614f0adfd2e9a08b79c5b34b52316a_hex);

    // two other updates for next batch
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[2].first, kv[2].second),
        make_update(kv[3].first, kv[3].second));
    EXPECT_EQ(
        this->root_hash(),
        0xd339cf4033aca65996859d35da4612b642664cc40734dbdd40738aa47f1e3e44_hex);

    auto [leaf, res] =
        find_blocking(this->get_storage_pool(), this->root.get(), kv[0].first);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        (monad::byte_string_view{leaf->leaf_data(), leaf->leaf_len}),
        kv[0].second);
    std::tie(leaf, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), kv[1].first);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        (monad::byte_string_view{leaf->leaf_data(), leaf->leaf_len}),
        kv[1].second);
    std::tie(leaf, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), kv[2].first);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        (monad::byte_string_view{leaf->leaf_data(), leaf->leaf_len}),
        kv[2].second);
    std::tie(leaf, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), kv[3].first);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        (monad::byte_string_view{leaf->leaf_data(), leaf->leaf_len}),
        kv[3].second);
}

TYPED_TEST(TrieTest, inserts_shorter_leaf_data)
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0x1234567812345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeef_hex},
        {0x1234567822345678123456781234567812345678123456781234567812345678_hex,
         0xdeadbeefcafebabe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345671_hex,
         0xdeadcafe_hex},
        {0x1234567832345678123456781234567812345678123456781234567812345678_hex,
         0xdead_hex}};

    std::vector<Update> update_vec;
    std::ranges::transform(
        kv, std::back_inserter(update_vec), [](auto &su) -> Update {
            auto &[k, v] = su;
            return make_update(k, monad::byte_string_view{v});
        });
    this->root =
        upsert_vector(this->update_aux, nullptr, std::move(update_vec));
    EXPECT_EQ(
        this->root_hash(),
        0xb796133251968233b84f3fcf8af88cdb42eeabe793f27835c10e8b46c91dfa4a_hex);
}

////////////////////////////////////////////////////////////////////
// Erase Trie Tests
////////////////////////////////////////////////////////////////////

TYPED_TEST(EraseTrieTest, none)
{
    EXPECT_EQ(
        this->root_hash(),
        0x22f3b7fc4b987d8327ec4525baf4cb35087a75d9250a8a3be45881dd889027ad_hex);
}

TYPED_TEST(EraseTrieTest, remove_everything)
{
    auto kv = fixed_updates::kv;

    std::vector<Update> update_vec;
    std::ranges::transform(
        kv, std::back_inserter(update_vec), [](auto &su) -> Update {
            auto &[k, v] = su;
            return make_erase(k);
        });
    this->root = upsert_vector(
        this->update_aux, this->root.get(), std::move(update_vec));
    EXPECT_EQ(
        this->root_hash(),
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}

TYPED_TEST(EraseTrieTest, delete_single_branch)
{
    auto kv = fixed_updates::kv;

    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_erase(kv[2].first),
        make_erase(kv[3].first));
    EXPECT_EQ(
        this->root_hash(),
        0x05a697d6698c55ee3e4d472c4907bca2184648bcfdd0e023e7ff7089dc984e7e_hex);
}

TYPED_TEST(EraseTrieTest, delete_one_at_a_time)
{
    auto kv = fixed_updates::kv;

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[2].first));
    EXPECT_EQ(
        this->root_hash(),
        0xd8b34a85db25148b1901459eac9805edadaa20b03f41fecd3b571f3b549e2774_hex);

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[1].first));
    EXPECT_EQ(
        this->root_hash(),
        0x107c8dd7bf9e7ca1faaa2c5856b412a8d7fccfa0005ca2500673a86b9c1760de_hex);

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[0].first));
    EXPECT_EQ(
        this->root_hash(),
        0x15fa9c02a40994d2d4f9c9b21daba3c4e455985490de5f9ae4889548f34d5873_hex);

    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[3].first));
    EXPECT_EQ(
        this->root_hash(),
        0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_hex);
}

TYPED_TEST(TrieTest, upsert_var_len_keys)
{
    // 2 accounts, kv[0] and kv[1]
    // kv[2,3,4] are of kv[0]'s storages
    // kv[5,6,7] are of kv[1]'s storages
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0x0a0b_hex}, // 0
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbcc_hex,
         0x1234_hex}, // 1
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbddaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0xbeef_hex}, // 2
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbddabcdaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xdeadbeef_hex}, // 3
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbddabcdeaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xcafe_hex}, // 4
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbccaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0xbeef_hex}, // 5
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbccabcdaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xdeadbeef_hex}, // 6
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbccabcdeaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xcafe_hex}}; // 7

    // insert kv 0,1
    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(kv[0].first, kv[0].second),
        make_update(kv[1].first, kv[1].second),
        make_update(kv[2].first, kv[2].second));
    EXPECT_EQ(
        this->root_hash(),
        0xd02534184b896dd4cb37fb34f176cafb508aa2ebc19a773c332514ca8c65ca10_hex);

    // update first trie's account value
    auto
        acc1 =
            0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
        new_val = 0x1234_hex;
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_update(acc1, new_val));
    EXPECT_EQ(
        this->root_hash(),
        0xe9e9d8bd0c74fe45b27ac36169fd6d58a0ee4eb6573fdf6a8680be814a63d2f5_hex);

    // update storages
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[3].first, kv[3].second));
    EXPECT_EQ(
        this->root_hash(),
        0xc2f4c0bf52f5b277252ecfe9df3c38b44d1787b3f89febde1d29406eb06e8f93_hex);

    // update storages again
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[4].first, kv[4].second));
    EXPECT_EQ(
        this->root_hash(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);

    // erase storage kv 3, 4
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_erase(kv[3].first),
        make_erase(kv[4].first));
    EXPECT_EQ(
        this->root_hash(),
        0xe9e9d8bd0c74fe45b27ac36169fd6d58a0ee4eb6573fdf6a8680be814a63d2f5_hex);

    // incarnation: now acc(kv[0]) only has 1 storage
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, new_val, true),
        make_update(kv[4].first, kv[4].second));
    EXPECT_EQ(
        this->root_hash(),
        0x2667b2bcc7c6a9afcd5a621be863fc06bf76022450e7e2e11ef792d63c7a689c_hex);

    // insert storages to the second account
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[5].first, kv[5].second),
        make_update(kv[6].first, kv[6].second),
        make_update(kv[7].first, kv[7].second));
    EXPECT_EQ(
        this->root_hash(),
        0x7954fcaa023fb356d6c626119220461c7859b93abd6ea71eac342d8407d7051e_hex);

    // erase all storages of kv[0].
    // TEMPORARY Note: when an existing account has no storages, the computed
    // leaf data is the input value, we don't concatenate with `empty_trie_hash`
    // in this poc impl yet.
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[4].first));
    EXPECT_EQ(
        this->root_hash(),
        0x055a9738d15fb121afe470905ca2254da172da7a188d8caa690f279c10422380_hex);

    // erase whole first account (kv[0])
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[0].first));
    EXPECT_EQ(
        this->root_hash(),
        0x2c077fecb021212686442677ecd59ac2946c34e398b723cf1be431239cb11858_hex);
}

TYPED_TEST(TrieTest, upsert_var_len_keys_nested)
{
    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0x0a0b_hex},
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbcc_hex,
         0x1234_hex}};
    std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0xbeef_hex},
        {0xabcdaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xdeadbeef_hex},
        {0xabcdeaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xcafe_hex}};

    Update a = make_update(storage_kv[0].first, storage_kv[0].second);
    UpdateList storage;
    storage.push_front(a);
    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(kv[0].first, kv[0].second, false, std::move(storage)),
        make_update(kv[1].first, kv[1].second));
    EXPECT_EQ(
        this->root_hash(),
        0xd02534184b896dd4cb37fb34f176cafb508aa2ebc19a773c332514ca8c65ca10_hex);

    // update first trie mid leaf data
    auto acc1 = kv[0].first, new_val = 0x1234_hex;
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_update(acc1, new_val));
    EXPECT_EQ(
        this->root_hash(),
        0xe9e9d8bd0c74fe45b27ac36169fd6d58a0ee4eb6573fdf6a8680be814a63d2f5_hex);

    // update storages
    Update b = make_update(storage_kv[1].first, storage_kv[1].second);
    storage.clear();
    storage.push_front(b);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0xc2f4c0bf52f5b277252ecfe9df3c38b44d1787b3f89febde1d29406eb06e8f93_hex);

    // update storage again
    Update c = make_update(storage_kv[2].first, storage_kv[2].second);
    storage.clear();
    storage.push_front(c);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);

    // erase some storage
    storage.clear();
    Update erase_b = make_erase(storage_kv[1].first),
           erase_c = make_erase(storage_kv[2].first);
    storage.push_front(erase_b);
    storage.push_front(erase_c);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0xe9e9d8bd0c74fe45b27ac36169fd6d58a0ee4eb6573fdf6a8680be814a63d2f5_hex);

    // incarnation
    storage.clear();
    storage.push_front(c);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, new_val, true, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0x2667b2bcc7c6a9afcd5a621be863fc06bf76022450e7e2e11ef792d63c7a689c_hex);

    // insert storages to the second account
    storage.clear();
    storage.push_front(a);
    storage.push_front(b);
    storage.push_front(c);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[1].first, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0x7954fcaa023fb356d6c626119220461c7859b93abd6ea71eac342d8407d7051e_hex);

    // erase all storages of kv[0].
    storage.clear();
    storage.push_front(erase_c);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(kv[0].first, std::move(storage)));
    EXPECT_EQ(
        this->root_hash(),
        0x055a9738d15fb121afe470905ca2254da172da7a188d8caa690f279c10422380_hex);

    // erase whole first account (kv[0])
    this->root = upsert_updates(
        this->update_aux, this->root.get(), make_erase(kv[0].first));
    EXPECT_EQ(
        this->root_hash(),
        0x2c077fecb021212686442677ecd59ac2946c34e398b723cf1be431239cb11858_hex);
}

TYPED_TEST(TrieTest, nested_updates_block_no)
{
    this->update_aux.sm = std::make_unique<StateMachineWithBlockNo>();

    const std::vector<std::pair<monad::byte_string, monad::byte_string>> kv{
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0x1234_hex},
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbcc_hex,
         0x1234_hex}};
    std::vector<std::pair<monad::byte_string, monad::byte_string>> storage_kv{
        {0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbdd_hex,
         0xbeef_hex},
        {0xabcdaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xdeadbeef_hex},
        {0xabcdeaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_hex,
         0xcafe_hex}};

    Update a = make_update(storage_kv[0].first, storage_kv[0].second),
           b = make_update(storage_kv[1].first, storage_kv[1].second),
           c = make_update(storage_kv[2].first, storage_kv[2].second);
    UpdateList storage;
    storage.push_front(a);
    storage.push_front(b);
    storage.push_front(c);
    UpdateList state_changes;
    Update s1 = make_update(
               kv[0].first, kv[0].second, false, std::move(storage)),
           s2 = make_update(kv[1].first, kv[1].second);
    state_changes.push_front(s1);
    state_changes.push_front(s2);
    auto blockno = 0x00000001_hex;
    auto blockno2 = 0x00000002_hex;
    this->root = upsert_updates(
        this->update_aux,
        nullptr,
        make_update(blockno, {}, false, std::move(state_changes)));
    auto [state_root, res] =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);

    // copy state root to blockno2
    this->root =
        copy_node(this->update_aux, std::move(this->root), blockno, blockno2);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(blockno2, monad::byte_string_view{}));

    std::tie(state_root, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno2);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);

    Node *old_state_root;
    std::tie(old_state_root, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(old_state_root->next_j(0), nullptr);
    EXPECT_EQ(
        old_state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);

    // copy state root to blockno3, update blockno3's leaf data
    auto blockno3 = 0x00000003_hex;
    this->root =
        copy_node(this->update_aux, std::move(this->root), blockno2, blockno3);
    this->root = upsert_updates(
        this->update_aux,
        this->root.get(),
        make_update(blockno3, 0xdeadbeef03_hex));
    std::tie(state_root, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno3);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);
    EXPECT_EQ(state_root->leaf_view(), 0xdeadbeef03_hex);

    std::tie(state_root, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno2);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);
    EXPECT_EQ(state_root->leaf_view(), monad::byte_string_view{});

    // copy state root from blockno2 to blockno3 again. In on-disk trie case,
    // blockno2 leaf is on disk and blockno3 leaf in memory, find_blocking()
    // will read for leaf of blockno2, and update curr blockno3 leaf to the same
    // as blockno2.
    this->root =
        copy_node(this->update_aux, std::move(this->root), blockno2, blockno3);
    std::tie(state_root, res) =
        find_blocking(this->get_storage_pool(), this->root.get(), blockno3);
    EXPECT_EQ(res, monad::mpt::find_result::success);
    EXPECT_EQ(
        state_root->hash_view(),
        0x9050b05948c3aab28121ad71b3298a887cdadc55674a5f234c34aa277fbd0325_hex);
    // leaf data changed here
    EXPECT_EQ(state_root->leaf_view(), monad::byte_string_view{});
}