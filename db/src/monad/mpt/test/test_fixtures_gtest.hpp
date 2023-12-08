#pragma once

#include "test_fixtures_base.hpp"

#include "../../async/test/test_fixture.hpp"

namespace monad::test
{
    struct InMemoryTrieGTest : public InMemoryTrieBase<::testing::Test>
    {
        using InMemoryTrieBase<::testing::Test>::InMemoryTrieBase;
    };

    struct OnDiskTrieGTest : public OnDiskTrieBase<::testing::Test>
    {
        using OnDiskTrieBase<::testing::Test>::OnDiskTrieBase;
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
