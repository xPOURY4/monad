#include "fixture.h"

#include <runtime/math.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.h>

#include <gtest/gtest.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

TEST_F(RuntimeTest, Mul)
{
    auto f = wrap(mul<EVMC_CANCUN>);

    ASSERT_EQ(f(10, 10), 100);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_u256,
          0),
        0);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256,
          2),
        0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_u256);
}

TEST_F(RuntimeTest, UDiv)
{
    auto f = wrap(udiv<EVMC_CANCUN>);

    ASSERT_EQ(f(4, 2), 2);
    ASSERT_EQ(f(4, 3), 1);
    ASSERT_EQ(f(4, 5), 0);
    ASSERT_EQ(f(4, 0), 0);
    ASSERT_EQ(f(10, 10), 1);
    ASSERT_EQ(f(1, 2), 0);
}

TEST_F(RuntimeTest, SDiv)
{
    constexpr auto neg = [](auto n) { return -utils::uint256_t{n}; };

    auto f = wrap(sdiv<EVMC_CANCUN>);

    ASSERT_EQ(f(8, 2), 4);
    ASSERT_EQ(f(neg(4), 2), neg(2));
    ASSERT_EQ(f(neg(4), neg(2)), 2);
    ASSERT_EQ(f(100, 0), 0);
    ASSERT_EQ(f(neg(4378), 0), 0);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_u256,
          0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256),
        2);
}

TEST_F(RuntimeTest, UMod)
{
    auto f = wrap(umod<EVMC_CANCUN>);

    ASSERT_EQ(f(10, 3), 1);
    ASSERT_EQ(f(17, 5), 2);
    ASSERT_EQ(f(247893, 0), 0);
    ASSERT_EQ(
        f(0x00000FBFC7A6E43ECE42F633F09556EF460006AE023965495AE1F990468E3B58_u256,
          15),
        4);
}

TEST_F(RuntimeTest, SMod)
{
    auto f = wrap(smod<EVMC_CANCUN>);

    ASSERT_EQ(f(10, 3), 1);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8_u256,
          0),
        0);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF8_u256,
          0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFD_u256),
        0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_u256);
}

TEST_F(RuntimeTest, AddMod)
{
    auto f = wrap(addmod<EVMC_CANCUN>);

    ASSERT_EQ(f(10, 10, 8), 4);
    ASSERT_EQ(f(134, 378, 0), 0);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256,
          2,
          2),
        1);
}

TEST_F(RuntimeTest, MulMod)
{
    auto f = wrap(mulmod<EVMC_CANCUN>);

    ASSERT_EQ(f(10, 10, 8), 4);
    ASSERT_EQ(f(134, 378, 0), 0);
    ASSERT_EQ(
        f(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256,
          0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256,
          12),
        9);
}

TEST_F(RuntimeTest, ExpOld)
{
    auto f = wrap(exp<EVMC_TANGERINE_WHISTLE>);

    ctx_.gas_remaining = 0;
    ASSERT_EQ(f(100, 0), 1);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 10;
    ASSERT_EQ(f(10, 2), 100);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 20;
    ASSERT_EQ(
        f(3, 256),
        0xC7ADEEB80D4FFF81FED242815E55BC8375A205DE07597D51D2105F2F0730F401_u256);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 30;
    ASSERT_EQ(
        f(5, 65536),
        0x6170C9D4CF040C5B5B784780A1BD33BA7B6BB3803AA626C24C21067A267C0001_u256);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, ExpNew)
{
    auto f = wrap(exp<EVMC_CANCUN>);

    ctx_.gas_remaining = 0;
    ASSERT_EQ(f(100, 0), 1);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 50;
    ASSERT_EQ(f(10, 2), 100);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 100;
    ASSERT_EQ(
        f(3, 256),
        0xC7ADEEB80D4FFF81FED242815E55BC8375A205DE07597D51D2105F2F0730F401_u256);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    ctx_.gas_remaining = 150;
    ASSERT_EQ(
        f(5, 65536),
        0x6170C9D4CF040C5B5B784780A1BD33BA7B6BB3803AA626C24C21067A267C0001_u256);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, SignExtend)
{
    auto f = wrap(signextend<EVMC_CANCUN>);

    ASSERT_EQ(
        f(0, 0xFF),
        0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256);
    ASSERT_EQ(f(0, 0x7F), 0x7F);
}
