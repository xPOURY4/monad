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
}
