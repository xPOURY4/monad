#include "runtime_fixture.h"

#include <runtime/math.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.h>

#include <gtest/gtest.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

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
