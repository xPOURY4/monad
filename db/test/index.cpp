#include "gtest/gtest.h"

#include <monad/trie/index.hpp>

using namespace monad::trie;

struct IndexTestFixture : public testing::Test
{
protected:
public:
    index_t index;
    IndexTestFixture(std::filesystem::path p = "index_test.db")
        : index(p)
    {
    }
};

TEST_F(IndexTestFixture, write_single)
{
    uint64_t vid = 1;
    uint64_t root_off = 123456;
    index.write_record(vid, root_off);

    EXPECT_EQ(index.get_history_root_off(vid), root_off);
}

TEST_F(IndexTestFixture, write_multiple)
{
    index.write_record(100, 123450);
    index.write_record(200, 123453);

    EXPECT_EQ(index.get_history_root_off(100), 123450);
    EXPECT_EQ(index.get_history_root_off(200), 123453);
}

TEST_F(IndexTestFixture, write_wraparound_overwrite)
{

    uint64_t vid = 100;
    uint64_t new_vid = vid + index.get_num_slots();
    uint64_t root_off = 123456, new_root_off = 234567;

    index.write_record(vid, root_off);
    EXPECT_EQ(index.get_history_root_off(vid), root_off);

    index.write_record(new_vid, new_root_off);

    // vid record has been overwritten, thus nullopt
    EXPECT_EQ(index.get_history_root_off(vid), std::nullopt);
    EXPECT_EQ(index.get_history_root_off(new_vid), new_root_off);
}