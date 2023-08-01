#include <gtest/gtest.h>

#include <monad/core/unordered_map.hpp>

TEST(UnorderedNodeMap, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_node_map<int, int> map;
    map[5] = 6;
    EXPECT_EQ(map[5], 6);
}

TEST(UnorderedDenseMap, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_dense_map<int, int> map;
    map[5] = 6;
    EXPECT_EQ(map[5], 6);
}

TEST(UnorderedFlatMap, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_flat_map<int, int> map;
    map[5] = 6;
    EXPECT_EQ(map[5], 6);
}

TEST(UnorderedNodeSet, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_node_set<int> set;
    set.insert(5);
    EXPECT_TRUE(set.contains(5));
}

TEST(UnorderedDenseSet, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_dense_set<int> set;
    set.insert(5);
    EXPECT_TRUE(set.contains(5));
}

TEST(UnorderedFlatSet, works)
{
    using namespace MONAD_NAMESPACE;
    unordered_flat_set<int> set;
    set.insert(5);
    EXPECT_TRUE(set.contains(5));
}
