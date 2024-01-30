#pragma once

#include "test_fixtures_base.hpp"

#include "../../async/test/test_fixture.hpp"

namespace monad::test
{
    struct InMemoryMerkleTrieGTest
        : public MerkleTrie<InMemoryTrieBase<void, ::testing::Test>>
    {
        using MerkleTrie<
            InMemoryTrieBase<void, ::testing::Test>>::InMemoryTrieBase;
    };

    struct OnDiskMerkleTrieGTest
        : public MerkleTrie<OnDiskTrieBase<void, ::testing::Test>>
    {
        using MerkleTrie<OnDiskTrieBase<void, ::testing::Test>>::OnDiskTrieBase;
    };

    struct InMemoryTrieGTest
        : public PlainTrie<InMemoryTrieBase<void, ::testing::Test>>
    {
    };

    struct OnDiskTrieGTest
        : public PlainTrie<OnDiskTrieBase<void, ::testing::Test>>
    {
    };

    template <
        size_t chunks_to_fill, bool alternate_slow_fast_writer = false,
        monad::mpt::lockable_or_void LockType = void>
    struct FillDBWithChunksGTest
        : public FillDBWithChunks<
              chunks_to_fill, alternate_slow_fast_writer, LockType,
              ::testing::Test>
    {
        using FillDBWithChunks<
            chunks_to_fill, alternate_slow_fast_writer, LockType,
            ::testing::Test>::FillDBWithChunks;
    };

}
