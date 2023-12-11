#pragma once

#include "test_fixtures_base.hpp"

#include "../../async/test/test_fixture.hpp"

namespace monad::test
{
    struct InMemoryMerkleTrieGTest
        : public MerkleTrie<InMemoryTrieBase<::testing::Test>>
    {
        using MerkleTrie<InMemoryTrieBase<::testing::Test>>::InMemoryTrieBase;
    };

    struct OnDiskMerkleTrieGTest
        : public MerkleTrie<OnDiskTrieBase<::testing::Test>>
    {
        using MerkleTrie<OnDiskTrieBase<::testing::Test>>::OnDiskTrieBase;
    };

    struct InMemoryTrieGTest
        : public PlainTrie<InMemoryTrieBase<::testing::Test>>
    {
    };

    struct OnDiskTrieGTest : public PlainTrie<OnDiskTrieBase<::testing::Test>>
    {
    };

    template <size_t chunks_to_fill, bool alternate_slow_fast_writer = false>
    struct FillDBWithChunksGTest
        : public FillDBWithChunks<
              chunks_to_fill, alternate_slow_fast_writer, ::testing::Test>
    {
        using FillDBWithChunks<
            chunks_to_fill, alternate_slow_fast_writer,
            ::testing::Test>::FillDBWithChunks;
    };

}
