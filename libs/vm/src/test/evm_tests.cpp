#include "evm_fixture.h"

#include <compiler/evm_opcodes.h>

#include <evmc/evmc.hpp>

#include <cstdint>
#include <evmc/evmc.h>
#include <vector>

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

// https://github.com/category-labs/monad-compiler/issues/138
TEST_F(EvmTest, BeaconRootRegression_138)
{
    using namespace evmc::literals;

    msg_.sender = 0xbe862ad9abfe6f22bcb087716c7d89a26051f74c_address;

    auto insts = std::vector<std::uint8_t>{{CALLER, PUSH20}};

    for (auto b : msg_.sender.bytes) {
        insts.push_back(b);
    }

    for (auto b : std::vector<std::uint8_t>{
             EQ, PUSH1, 0x1D, JUMPI, PUSH0, PUSH0, REVERT, JUMPDEST, STOP}) {
        insts.push_back(b);
    }

    ASSERT_EQ(insts[2], 0xBE);
    ASSERT_EQ(insts[21], 0x4C);
    execute(insts);

    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
}
