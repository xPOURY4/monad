#pragma once

#include "test_fixtures_base.hpp"

#include <gtest/gtest.h>

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
