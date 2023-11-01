#include <monad/mpt/update.hpp>

#include "../test_fixtures.hpp"
#include "in_memory_comparator.hpp"
#include "one_hundred_updates.hpp"

#include <monad/core/byte_string.hpp>

#include <fuzztest/fuzztest.h>
#include <fuzztest/googletest_fixture_adapter.h>
#include <gtest/gtest.h>

#include <random>

using namespace fuzztest;
using namespace monad;
using namespace monad::test;

template <typename TFixture>
struct trie_fuzzer_fixture
    : public ::fuzztest::PerIterationFixtureAdapter<TFixture>
{
    using TFixture::root_hash;

#ifndef NDEBUG
    void SimpleStraight()
    {
        std::vector<Update> updates;
        for (auto &i : one_hundred_updates) {
            updates.emplace_back(make_update(i.first, i.second));
        }
        this->root = upsert_vector(
            this->update_aux, this->root.get(), std::move(updates));

        EXPECT_EQ(
            root_hash(),
            0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);
    }

    void SimplePermuted(uint32_t seed)
    {
        std::vector<Update> updates;
        for (auto &i : one_hundred_updates) {
            updates.emplace_back(make_update(i.first, i.second));
        }
        auto rng = std::minstd_rand{seed};
        std::shuffle(updates.begin(), updates.end(), rng);
        this->root = upsert_vector(
            this->update_aux, this->root.get(), std::move(updates));

        EXPECT_EQ(
            root_hash(),
            0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);
    }
#endif

    void OneHundredUpdates(
        std::array<size_t, 100> const &groups,
        std::map<size_t, std::optional<monad::byte_string>> const &mods)
    {
        assert(this->root.get() == nullptr);
        Process(one_hundred_updates, groups, mods);

        if (!mods.empty()) {
            // Fix the trie so that the root hashes are back to normal
            std::vector<Update> correct;
            for (auto const &[i, _] : mods) {
                // overwrite mods values to final ones
                correct.emplace_back(make_update(
                    one_hundred_updates[i].first,
                    one_hundred_updates[i].second));
            }
            this->root = upsert_vector(
                this->update_aux, this->root.get(), std::move(correct));
        }
        EXPECT_EQ(
            root_hash(),
            0xcbb6d81afdc76fec144f6a1a283205d42c03c102a94fc210b3a1bcfdcb625884_hex);
    }

    void GeneratedKv(
        std::map<monad::byte_string, monad::byte_string> const &kv,
        std::vector<size_t> const &groups,
        std::map<size_t, std::optional<monad::byte_string>> const &mods)
    {
        assert(this->root.get() == nullptr);
        std::vector<std::pair<monad::byte_string, monad::byte_string>>
            transformed;
        for (auto const &p : kv) {
            transformed.emplace_back(
                [&]() {
                    MONAD_ASSERT(p.first.size() == 32);
                    monad::byte_string b;
                    b.resize(32);
                    std::copy_n(p.first.data(), 32, b.data());
                    return b;
                }(),
                p.second);
        }
        Process(transformed, groups, mods);

        std::vector<Update> updates;
        for (size_t i = 0; i < transformed.size(); ++i) {
            if (mods.contains(i) && !mods.at(i).has_value()) {
                continue;
            }
            updates.emplace_back(make_erase(transformed[i].first));
        }

        if (!updates.empty()) {
            this->root = upsert_vector(
                this->update_aux, this->root.get(), std::move(updates));
        }
        EXPECT_EQ(root_hash(), NULL_ROOT);
    }

    void Process(
        std::ranges::range auto const &kv,
        std::ranges::range auto const &groups,
        std::map<size_t, std::optional<monad::byte_string>> const &mods)
    {
        MONAD_ASSERT(groups.size() == kv.size());

        std::map<size_t, std::vector<Update>> inputs;
        inputs.emplace(kv.size(), std::vector<Update>{});

        for (size_t i = 0; i < groups.size(); ++i) {
            inputs[groups[i]].emplace_back(
                make_update(kv[i].first, kv[i].second));
        }

        // Add updates to the next batch of inputs to be processed
        for (size_t i = 0; i < kv.size(); ++i) {
            if (!mods.contains(i)) {
                continue;
            }

            auto const it = inputs.find(groups[i]);
            MONAD_DEBUG_ASSERT(it != inputs.end());

            auto const next = std::next(it);
            MONAD_DEBUG_ASSERT(next != inputs.end());

            if (mods.at(i).has_value()) {
                // insert kv of {key[i], random generated value in mods}
                next->second.emplace_back(
                    make_update(kv[i].first, mods.at(i).value()));
            }
            else {
                next->second.emplace_back(make_erase(kv[i].first));
            }
        }

        if (inputs[kv.size()].empty()) {
            inputs.erase(kv.size());
        }

        size_t count = 0;
        for (auto &[_, input] : inputs) {
            count += input.size();
            this->root = upsert_vector(
                this->update_aux, this->root.get(), std::move(input));
        }
        MONAD_ASSERT(count >= 100);
    }
};

using in_memory_trie_fixture_t = trie_fuzzer_fixture<InMemoryTrie>;

#ifndef NDEBUG
FUZZ_TEST_F(in_memory_trie_fixture_t, SimpleStraight);

FUZZ_TEST_F(in_memory_trie_fixture_t, SimplePermuted)
    .WithDomains(Arbitrary<uint32_t>());
#endif

inline constexpr auto MAX_VALUE_SIZE = 110u;
FUZZ_TEST_F(in_memory_trie_fixture_t, OneHundredUpdates)
    .WithDomains(
        ArrayOf<one_hundred_updates.size()>(
            InRange(0ul, one_hundred_updates.size() - 1)),
        MapOf(
            InRange(0ul, one_hundred_updates.size() - 1),
            OptionalOf(Arbitrary<monad::byte_string>()
                           .WithMinSize(1)
                           .WithMaxSize(MAX_VALUE_SIZE))));

inline constexpr auto GENERATED_SIZE = 100ul;

// Note: depending on the GENERATED_SIZE, GeneratedKv may run out of stack
// memory. To permiss the test a larger stack, set the environment variable
// FUZZTEST_STACK_LIMIT.
FUZZ_TEST_F(in_memory_trie_fixture_t, GeneratedKv)
    .WithDomains(
        MapOf(
            Arbitrary<monad::byte_string>().WithSize(32),
            NonEmpty(
                Arbitrary<monad::byte_string>().WithMaxSize(MAX_VALUE_SIZE)))
            .WithSize(GENERATED_SIZE),
        VectorOf(InRange(0ul, GENERATED_SIZE - 1)).WithSize(GENERATED_SIZE),
        MapOf(
            InRange(0ul, GENERATED_SIZE - 1),
            OptionalOf(NonEmpty(
                Arbitrary<monad::byte_string>().WithMaxSize(MAX_VALUE_SIZE)))));
