#include "evm_fixture.hpp"

#include <monad/vm/code.hpp>
#include <monad/vm/compiler.hpp>
#include <monad/vm/compiler/types.hpp>
#include <monad/vm/evm/opcodes.hpp>

#include <test_resource_data.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

using namespace monad::vm;
using namespace monad::vm::compiler;
using namespace monad::vm::compiler::test;

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
    ASSERT_EQ(result_.status_code, EVMC_FAILURE);
}

// https://github.com/category-labs/monad-compiler/issues/192
TEST_F(EvmTest, BadJumpRegression_192)
{
    execute({PUSH0, JUMP});
    ASSERT_EQ(result_.status_code, EVMC_FAILURE);
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
    ASSERT_EQ(uint256_t::load_be_unsafe(result_.output_data), uint256_t{98});
}

TEST_F(EvmTest, JumpiLiveDestDeferredComparisonBug)
{
    execute(
        1000,
        {JUMPDEST,
         GAS,
         ADDRESS,
         ADD,
         PUSH1,
         0xf9,
         SHL,
         ADDRESS,
         ADDRESS,
         SLT,
         JUMPI});
    ASSERT_EQ(result_.status_code, EVMC_FAILURE);
}

TEST_F(EvmTest, Cmov32BitBug)
{
    execute(
        1000,
        {PUSH1,
         0x60,
         PUSH1,
         0x02,
         EXP,
         PUSH1,
         0x30,
         DUP2,
         SAR,
         ADDRESS,
         JUMPI});
    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
}

TEST_F(EvmTest, MissingDischargeInJumpiKeepFallthroughStack)
{
    std::vector<uint8_t> bytecode{
        0x60, 0x80, 0x60, 0x40, 0x52, 0x34, 0x80, 0x15, 0x60, 0x00, 0x38, 0x57,
        0x80, 0xfd, 0x5b, 0x50, 0x61, 0x01, 0xf7, 0x80, 0x61, 0x00, 0x1c, 0x5f,
        0x39, 0x5f, 0xf3, 0xfe, 0x60, 0x80, 0x60, 0x40, 0x52, 0x34, 0x80, 0x15,
        0x61, 0x00, 0x0f, 0x57, 0x5f, 0x80, 0xfd, 0x5b, 0x50, 0x60, 0x04, 0x36,
        0x10, 0x61, 0x00, 0x34, 0x57, 0x5f, 0x35, 0x60, 0xe0, 0x1c, 0x80, 0x63,
        0xb3, 0xde, 0x64, 0x8b, 0x14, 0x61, 0x0e, 0x57, 0x5f, 0x80, 0x63, 0xe4,
        0x20, 0x26, 0x4a, 0x14, 0x61, 0x00, 0x6a, 0x57, 0x5b, 0x5f, 0x80, 0xfd,
        0x5b, 0x61, 0x00, 0x52, 0x60, 0x04, 0x80, 0x36, 0x03, 0x81, 0x01, 0x90,
        0x61, 0x00, 0x4d, 0x91, 0x90, 0x61, 0x01, 0x52, 0x56, 0x5b, 0x61, 0x00,
        0x9c, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x61, 0x00, 0x61, 0x93, 0x92, 0x91,
        0x90, 0x61, 0x01, 0x8c, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x81, 0x90, 0x39,
        0x0f, 0x35, 0x5b, 0x61, 0x00, 0x84, 0x60, 0x04, 0x80, 0x36, 0x03, 0x81,
        0x01, 0x90, 0x61, 0x00, 0x7f, 0x91, 0x90, 0x61, 0x01, 0x52, 0x56, 0x5b,
        0x61, 0x00, 0xdb, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x61, 0x00, 0x93, 0x93,
        0x92, 0x91, 0x90, 0x61, 0x01, 0x8c, 0x56, 0x5b, 0x60, 0x40, 0x51, 0x81,
        0x90, 0x39, 0x0f, 0x35, 0x5b, 0x5f, 0x80, 0x5f, 0x80, 0x60, 0xf8, 0x85,
        0x90, 0x1b, 0x90, 0x50, 0x80, 0x5f, 0x1a, 0x90, 0x50, 0x5f, 0x60, 0x08,
        0x86, 0x90, 0x1b, 0x90, 0x50, 0x80, 0x60, 0x1e, 0x1a, 0x90, 0x50, 0x5f,
        0x60, 0x10, 0x87, 0x90, 0x1b, 0x90, 0x50, 0x80, 0x60, 0x11, 0x1a, 0x90,
        0x50, 0x82, 0x82, 0x82, 0x95, 0x50, 0x95, 0x50, 0x1b, 0x90, 0x50, 0x80,
        0x5f, 0x1a, 0x90, 0x5a, 0x5f, 0x60, 0x08, 0x86, 0x90, 0x1b, 0x90, 0x50,
        0x85, 0x90, 0x1c, 0x90, 0x50, 0x80, 0x60, 0x1f, 0x1a, 0x90, 0x50, 0x5f,
        0x60, 0x08, 0x86, 0x90, 0x1c, 0x90, 0x50, 0x80, 0x60, 0x04, 0x1a, 0x90,
        0x50, 0x5f, 0x60, 0x10};
    execute_and_compare(1'000'000, bytecode, {});
}

TEST_F(EvmTest, WrongGasCheckConditionalJump)
{
    std::vector<uint8_t> bytecode{
        0x60, 0x80, 0x60, 0x40, 0x52, 0x34, 0x80, 0x15, 0x60, 0x0e, 0x57, 0x5f,
        0x80, 0xfd, 0x5b, 0x50, 0x60, 0x04, 0x36, 0x10, 0x60, 0x26, 0x57, 0x5f,
        0x35, 0x60, 0xe0, 0x06, 0x60, 0x40, 0x52, 0x34, 0x80, 0x15, 0x60, 0x0e,
        0x57, 0x5f, 0x80, 0xfd, 0x5b, 0x50, 0x60, 0x04, 0x36, 0x10, 0x60, 0x26,
        0x57, 0x5f, 0x35, 0x60, 0xe0, 0x01, 0xc8, 0x80, 0x63, 0x26, 0x12, 0x1f,
        0xf0, 0x14, 0x60, 0x2a, 0x57, 0xb5, 0x5f, 0x80, 0xfd, 0x5b, 0x60, 0x30,
        0x60, 0x32, 0x56, 0x5b, 0x00, 0x5b, 0x56, 0xfe, 0xa2, 0x64, 0x69, 0x78,
        0x06, 0x73, 0x58, 0x22, 0x12, 0x20, 0xaa, 0xfb, 0xea, 0x54, 0x7b, 0x5a,
        0x65, 0x1b, 0x3b, 0x1a, 0x08, 0x4f, 0xb0, 0xbb, 0x77, 0x34, 0xdc, 0x44,
        0x12, 0xf0, 0x0d, 0xd0, 0x8c, 0x92, 0x19, 0xa1, 0xcb, 0x85, 0x07, 0x9b,
        0x3e, 0x86, 0x47, 0x36, 0xf6, 0xc6, 0x34, 0x30};

    std::vector<uint8_t> calldata{
        0x26, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    execute_and_compare(1'000'000, bytecode, calldata);
}

TEST_F(EvmTest, MissingRemoveStackOffsetInFallthroughStack)
{
    std::vector<uint8_t> bytecode{
        0x60, 0x80, 0x60, 0x40, 0x52, 0x60, 0x01, 0x5f, 0x55, 0x60, 0x02, 0x60,
        0x01, 0x55, 0x34, 0x80, 0x15, 0x60, 0x17, 0x57, 0x5f, 0x80, 0xfd, 0x5b,
        0x50, 0x5f, 0x54, 0x5f, 0x54, 0x60, 0x24, 0x91, 0x90, 0x60, 0x76, 0x56,
        0x5b, 0x5f, 0x80, 0x00, 0x00, 0x05, 0xf5, 0x54, 0x60, 0x01, 0x54, 0x60,
        0x36, 0x91, 0x90, 0x60, 0xa2, 0x56, 0x5b, 0x60, 0x01, 0x81, 0x90, 0x55,
        0x50, 0x60, 0xce, 0x56, 0x5b, 0x5f, 0x81, 0x90, 0x50, 0x91, 0x90, 0x50,
        0x56, 0x5b, 0x7f, 0x4e, 0x48, 0x7b, 0x71, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19, 0x05, 0x55, 0x05,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x52, 0x60, 0x24, 0x5f, 0xfd, 0x5b, 0x5f,
        0x60, 0x7e, 0x82, 0x60, 0x40, 0x56, 0x5b, 0x91, 0x50, 0x60, 0x87, 0x83,
        0x33, 0x33, 0x33, 0x33, 0x34, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
        0x44, 0x44, 0x44, 0x44, 0x9c, 0x57, 0x60, 0x9b, 0x60, 0x49, 0x56, 0x5b,
        0x5b, 0x92, 0x91, 0x50, 0x50, 0x56, 0x5b, 0x5f, 0x60};

    std::vector<uint8_t> calldata{0xe5, 0xaa, 0x3d, 0x58, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    execute_and_compare(1'000'000, bytecode, calldata);
}

TEST_F(EvmTest, DupStackOverflow)
{
    auto bytecode = std::vector<std::uint8_t>{};
    std::fill_n(std::back_inserter(bytecode), 1024, GAS);
    bytecode.push_back(DUP4);

    execute(bytecode, {}, Implementation::Interpreter);

    ASSERT_EQ(result_.status_code, EVMC_FAILURE);
}

TEST_F(EvmTest, NativeCodeSizeOutOfBound)
{
    std::vector<uint8_t> bytecode;
    CompilerConfig const config{.max_code_size_offset = 1024};
    bytecode.push_back(PUSH1);
    bytecode.push_back(1);
    bytecode.push_back(PUSH1);
    bytecode.push_back(2);
    bytecode.push_back(PUSH1);
    bytecode.push_back(3);
    for (size_t i = 0; i < config.max_code_size_offset; ++i) {
        bytecode.push_back(JUMPI);
    }
    bytecode.push_back(JUMPDEST);
    auto icode = make_shared_intercode(bytecode);
    auto ncode = vm_.compiler().compile(EVMC_CANCUN, icode, config);
    ASSERT_EQ(ncode->error_code(), Nativecode::ErrorCode::SizeOutOfBound);
    ASSERT_GT(ncode->code_size_estimate_before_error(), 1024 * 32);
}

TEST_F(EvmTest, MaxDeltaOutOfBound)
{
    CompilerConfig const config{.max_code_size_offset = 32 * 1024};

    std::vector<uint8_t> base_bytecode;
    for (size_t i = 0; i < 1024; ++i) {
        base_bytecode.push_back(PUSH9);
        base_bytecode.push_back(1 + static_cast<uint8_t>(i >> 8));
        base_bytecode.push_back(static_cast<uint8_t>(i & 255));
        for (int j = 0; j < 7; ++j) {
            base_bytecode.push_back(0);
        }
    }
    std::vector<uint8_t> bytecode1{base_bytecode};
    bytecode1.push_back(JUMPDEST);
    auto const icode1 = make_shared_intercode(bytecode1);
    auto const ncode1 = vm_.compiler().compile(EVMC_CANCUN, icode1, config);

    pre_execute(10'000, {});
    result_ = vm_.execute_native_entrypoint(
        &host_.get_interface(),
        host_.to_context(),
        &msg_,
        icode1,
        ncode1->entrypoint());

    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result_.gas_left, 10'000 - (3 * 1024 + 1));

    std::vector<uint8_t> bytecode2{base_bytecode};
    bytecode2.push_back(PUSH0);
    bytecode2.push_back(JUMPDEST);
    auto const icode2 = make_shared_intercode(bytecode2);
    auto const ncode2 = vm_.compiler().compile(EVMC_CANCUN, icode2, config);

    pre_execute(10'000, {});
    result_ = vm_.execute_native_entrypoint(
        &host_.get_interface(),
        host_.to_context(),
        &msg_,
        icode2,
        ncode2->entrypoint());

    ASSERT_EQ(result_.status_code, EVMC_FAILURE);

    // Since the basic block in `ncode2` is known to overflow the stack, with
    // max_delta > 1024, the native code for the basic block should just jump
    // to the error label, without block prologue/epilogue and without the
    // pushes to the evm stack inside the basic block.
    ASSERT_LT(
        ncode2->code_size_estimate() + 32 * 1024, ncode1->code_size_estimate());
}

TEST_F(EvmTest, MinDeltaOutOfBound)
{
    CompilerConfig const config{.max_code_size_offset = 32 * 1024};

    std::vector<uint8_t> base_bytecode;
    for (size_t i = 0; i < 1024; ++i) {
        base_bytecode.push_back(CODESIZE);
    }
    base_bytecode.push_back(JUMPDEST);
    for (size_t i = 0; i < 1024; ++i) {
        base_bytecode.push_back(POP);
    }
    std::vector<uint8_t> bytecode1{base_bytecode};
    bytecode1.push_back(JUMPDEST);
    auto const icode1 = make_shared_intercode(bytecode1);
    auto const ncode1 = vm_.compiler().compile(EVMC_CANCUN, icode1, config);

    pre_execute(10'000, {});
    result_ = vm_.execute_native_entrypoint(
        &host_.get_interface(),
        host_.to_context(),
        &msg_,
        icode1,
        ncode1->entrypoint());

    ASSERT_EQ(result_.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result_.gas_left, 10'000 - (2 * 1024 + 1 + 2 * 1024 + 1));

    std::vector<uint8_t> bytecode2{base_bytecode};
    bytecode2.push_back(POP);
    bytecode2.push_back(JUMPDEST);
    auto const icode2 = make_shared_intercode(bytecode2);
    auto const ncode2 = vm_.compiler().compile(EVMC_CANCUN, icode2, config);

    pre_execute(10'000, {});
    result_ = vm_.execute_native_entrypoint(
        &host_.get_interface(),
        host_.to_context(),
        &msg_,
        icode2,
        ncode2->entrypoint());

    ASSERT_EQ(result_.status_code, EVMC_FAILURE);

    // We expect native code size of `ncode2` to be smaller, because the last
    // basic block has min_delta < -1024, so will just jump to error label,
    // without basic block prologue/epilogue.
    ASSERT_LT(ncode2->code_size_estimate(), ncode1->code_size_estimate());
}

INSTANTIATE_TEST_SUITE_P(
    EvmTest, EvmFile,
    ::testing::ValuesIn(std::vector<fs::directory_entry>{
        fs::directory_iterator(test_resource::regression_tests_dir), {}}),
    [](auto const &info) { return info.param.path().stem().string(); });
