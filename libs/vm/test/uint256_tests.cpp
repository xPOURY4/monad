#include <monad/utils/uint256.hpp>

#include <gtest/gtest.h>

using namespace monad::utils;

TEST(uint256, signextend)
{
    uint256_t i;
    uint256_t x;

    i = 0;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), 0);

    i = 1;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), ~uint256_t{0xffff} | x);

    i = 2;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), ~uint256_t{0xffffff} | x);

    i = 3;
    x = 0xff8000;
    ASSERT_EQ(signextend(i, x), x);

    i = 30;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(signextend(i, x), uint256_t{0xff80} << 240);

    i = 30;
    x = uint256_t{0x0070} << 240;
    ASSERT_EQ(signextend(i, x), uint256_t{0x0070} << 240);

    i = 31;
    x = uint256_t{0xf0} << 248;
    ASSERT_EQ(signextend(i, x), uint256_t{0xf0} << 248);
}

TEST(uint256, byte)
{
    uint256_t i;
    uint256_t x;

    i = 31;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0);

    i = 30;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0x80);

    i = 29;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0xff);

    i = 28;
    x = 0xff8000;
    ASSERT_EQ(byte(i, x), 0);

    i = 1;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(byte(i, x), 0x80);

    i = 0;
    x = uint256_t{0x0080} << 240;
    ASSERT_EQ(byte(i, x), 0);

    i = 0;
    x = uint256_t{0xf0} << 248;
    ASSERT_EQ(byte(i, x), 0xf0);

    i = 32;
    x = uint256_t{0xff} << 248;
    ASSERT_EQ(byte(i, x), 0);
}

TEST(uint256, sar)
{
    uint256_t i;
    uint256_t x;

    i = 0;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), x);

    i = 1;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), uint256_t{0xc0} << 248);

    i = 1;
    x = uint256_t{0x70} << 248;
    ASSERT_EQ(sar(i, x), uint256_t{0x38} << 248);

    i = 255;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), ~uint256_t{0});

    i = 254;
    x = uint256_t{0x80} << 248;
    ASSERT_EQ(sar(i, x), ~uint256_t{0} - 1);

    i = 254;
    x = uint256_t{0x40} << 248;
    ASSERT_EQ(sar(i, x), 1);

    i = 255;
    x = uint256_t{0x7f} << 248;
    ASSERT_EQ(sar(i, x), 0);
}
