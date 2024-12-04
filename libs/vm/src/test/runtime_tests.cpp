#include "runtime_fixture.h"

#include <runtime/math.h>

#include <evmc/evmc.h>

#include <gtest/gtest.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

TEST_F(RuntimeTest, UDiv)
{
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 2), 2);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 3), 1);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 5), 0);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 0), 0);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 10, 10), 1);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 1, 2), 0);
}

TEST_F(RuntimeTest, SDiv)
{
    constexpr auto neg = [](auto n) { return -utils::uint256_t{n}; };

    ASSERT_EQ(call(sdiv<EVMC_CANCUN>, 8, 2), 4);
    ASSERT_EQ(call(sdiv<EVMC_CANCUN>, neg(4), 2), neg(2));
    ASSERT_EQ(call(sdiv<EVMC_CANCUN>, neg(4), neg(2)), 2);
    ASSERT_EQ(call(sdiv<EVMC_CANCUN>, 100, 0), 0);
    ASSERT_EQ(call(sdiv<EVMC_CANCUN>, neg(4378), 0), 0);
    ASSERT_EQ(
        call(
            sdiv<EVMC_CANCUN>,
            0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE_u256,
            0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_u256),
        2);
}

TEST_F(RuntimeTest, UMod)
{
    ASSERT_EQ(call(umod<EVMC_CANCUN>, 10, 3), 1);
    ASSERT_EQ(call(umod<EVMC_CANCUN>, 17, 5), 2);
    ASSERT_EQ(call(umod<EVMC_CANCUN>, 247893, 0), 0);
    ASSERT_EQ(
        call(
            umod<EVMC_CANCUN>,
            0x00000FBFC7A6E43ECE42F633F09556EF460006AE023965495AE1F990468E3B58_u256,
            15),
        4);
}
