#include "evm_fixture.h"
#include "test_resource_data.h"

#include <compiler/evm_opcodes.h>
#include <compiler/types.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

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

// https://github.com/category-labs/monad-compiler/issues/190
TEST_F(EvmTest, UnderflowRegression_190)
{
    execute({POP});
    ASSERT_EQ(result_.status_code, EVMC_STACK_UNDERFLOW);
}

// https://github.com/category-labs/monad-compiler/issues/192
TEST_F(EvmTest, BadJumpRegression_192)
{
    execute({PUSH0, JUMP});
    ASSERT_EQ(result_.status_code, EVMC_BAD_JUMP_DESTINATION);
}

TEST_P(EvmFile, RegressionFile)
{
    auto const entry = GetParam();
    auto file = std::ifstream{entry.path(), std::ifstream::binary};

    ASSERT_TRUE(file.good());

    std::vector<uint8_t> code(std::istreambuf_iterator<char>{file}, {});

    execute_and_compare(30'000'000, code);
}

TEST_F(EvmTest, SignextendLiveIndexBug)
{
    execute(
        100, {GAS, DUP1, SIGNEXTEND, PUSH0, MSTORE, PUSH1, 32, PUSH0, RETURN});
    ASSERT_EQ(result_.output_size, 32);
    ASSERT_EQ(
        intx::be::unsafe::load<uint256_t>(result_.output_data), uint256_t{98});
}

INSTANTIATE_TEST_SUITE_P(
    EvmTest, EvmFile,
    ::testing::ValuesIn(std::vector<fs::directory_entry>{
        fs::directory_iterator(test_resource::regression_tests_dir), {}}),
    [](auto const &info) { return info.param.path().stem().string(); });
