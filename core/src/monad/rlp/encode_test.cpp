#include <monad/rlp/encode.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>

TEST(rlp, impl_length_length)
{
    size_t result;

    result = monad::rlp::impl::length_length(0);
    EXPECT_EQ(result, 0);

    result = monad::rlp::impl::length_length(1);
    EXPECT_EQ(result, 1);

    result = monad::rlp::impl::length_length(255);
    EXPECT_EQ(result, 1);

    result = monad::rlp::impl::length_length(256);
    EXPECT_EQ(result, 2);

    result = monad::rlp::impl::length_length(65535);
    EXPECT_EQ(result, 2);

    result = monad::rlp::impl::length_length(65536);
    EXPECT_EQ(result, 3);

    result = monad::rlp::impl::length_length((1UL << 56) - 1);
    EXPECT_EQ(result, 7);

    result = monad::rlp::impl::length_length(1UL << 56);
    EXPECT_EQ(result, 8);

    result = monad::rlp::impl::length_length(0xFFFFFFFFFFFFFFFFUL);
    EXPECT_EQ(result, 8);
}

TEST(rlp, impl_encode_length)
{
    unsigned char buf[8];
    unsigned char *result;

    result = monad::rlp::impl::encode_length(buf, 0);
    EXPECT_EQ(result - buf, 0);

    result = monad::rlp::impl::encode_length(buf, 1);
    EXPECT_EQ(result - buf, 1);
    EXPECT_TRUE(std::equal(buf, result, (unsigned char[]){1}));

    result = monad::rlp::impl::encode_length(buf, 255);
    EXPECT_EQ(result - buf, 1);
    EXPECT_TRUE(std::equal(buf, result, (unsigned char[]){255}));

    result = monad::rlp::impl::encode_length(buf, 256);
    EXPECT_EQ(result - buf, 2);
    EXPECT_TRUE(std::equal(buf, result, (unsigned char[]){1, 0}));

    result = monad::rlp::impl::encode_length(buf, 258);
    EXPECT_EQ(result - buf, 2);
    EXPECT_TRUE(std::equal(buf, result, (unsigned char[]){1, 2}));

    result = monad::rlp::impl::encode_length(buf, 0xFFFFFFFFFFFFFFFFUL);
    EXPECT_EQ(result - buf, 8);
    EXPECT_TRUE(std::equal(
        buf,
        result,
        (unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}));
}
