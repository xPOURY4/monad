#include "evm_fixture.h"

#include <compiler/evm_opcodes.h>

#include <evmc/evmc.h>

using namespace monad::compiler;
using namespace monad::compiler::test;

TEST_F(EvmTest, Stop)
{
    execute(0, {STOP});
    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
}

TEST_F(EvmTest, Push0)
{
    execute(2, {PUSH0});
    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result_.gas_left, 0);
}

TEST_F(EvmTest, PushSeveral)
{
    execute(10, {PUSH1, 0x01, PUSH2, 0x20, 0x20, PUSH0});
    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result_.gas_left, 2);
}

TEST_F(EvmTest, OutOfGas)
{
    execute(6, {PUSH0, PUSH0, ADD});
    ASSERT_EQ(result_.status_code, EVMC_OUT_OF_GAS);
    ASSERT_EQ(result_.gas_left, 0);
}
