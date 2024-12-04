#include "runtime_fixture.h"

#include <runtime/math.h>

#include <evmc/evmc.h>

#include <gtest/gtest.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;

TEST_F(RuntimeTest, UDiv)
{
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 2), 2);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 3), 1);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 5), 0);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 4, 0), 0);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 10, 10), 1);
    ASSERT_EQ(call(udiv<EVMC_CANCUN>, 1, 2), 0);
}
