#include <compiler/ir/bytecode.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/registers.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <intx/intx.hpp>
#include <unordered_map>
#include <vector>

using namespace monad::compiler;
using namespace intx;

void tokens_eq(
    std::vector<uint8_t> const &in, std::vector<Token> const &expected)
{
    EXPECT_EQ(BytecodeIR(in).tokens, expected);
}

TEST(TokenTest, Formatter)
{
    EXPECT_EQ(std::format("{}", Token{4, PUSH1, 0x42}), "(4, PUSH1, 0x42)");
    EXPECT_EQ(
        std::format(
            "{}",
            Token{
                0,
                PUSH32,
                0xab00000000000000000000000000000000000000000000000000000000000000_u256}),
        "(0, PUSH32, "
        "0xab00000000000000000000000000000000000000000000000000000000000000)");
}

TEST(BytecodeTest, ToTokens)
{
    tokens_eq({}, {});
    tokens_eq({STOP}, {{0, STOP, 0}});
    tokens_eq({0xee}, {{0, 0xee, 0}});
    tokens_eq({PUSH0}, {{0, PUSH0, 0}});
    tokens_eq({PUSH1, 0xff}, {{0, PUSH1, 0xff}});
    tokens_eq({PUSH2, 0xff, 0xee}, {{0, PUSH2, 0xffee}});
    tokens_eq({PUSH1, 0xff, PUSH1, 0xee}, {{0, PUSH1, 0xff}, {2, PUSH1, 0xee}});
    tokens_eq(
        {STOP, PUSH2, 0xaa, 0xbb, 0xee},
        {{0, STOP, 0}, {1, PUSH2, 0xaabb}, {4, 0xee, 0}});
    tokens_eq({PUSH1}, {{0, PUSH1, 0x0}});
    tokens_eq({PUSH2, 0xff}, {{0, PUSH2, 0xff00}});
    tokens_eq({PUSH4, 0xaa, 0xbb}, {{0, PUSH4, 0xaabb0000}});

    tokens_eq(
        {PUSH32, 0xab},
        {{0,
          PUSH32,
          0xab00000000000000000000000000000000000000000000000000000000000000_u256}});
}

TEST(BytecodeTest, Formatter)
{
    EXPECT_EQ(std::format("{}", BytecodeIR({})), "bytecode:\n");
    EXPECT_EQ(
        std::format("{}", BytecodeIR({STOP})), "bytecode:\n  (0, STOP, 0x0)\n");
    EXPECT_EQ(
        std::format("{}", BytecodeIR({STOP, PUSH1, 0xab})),
        "bytecode:\n  (0, STOP, 0x0)\n  (1, PUSH1, 0xab)\n");
}

void blocks_eq(
    std::vector<uint8_t> const &in,
    std::unordered_map<byte_offset, block_id> const &expected_jumpdests,
    std::vector<Block> const &expected_blocks)
{
    BytecodeIR const actual_bc(in);
    InstructionIR const actual(actual_bc);

    EXPECT_EQ(actual.jumpdests, expected_jumpdests);
    EXPECT_EQ(actual.blocks, expected_blocks);
}

using Terminator::Jump;
using Terminator::JumpDest;
using Terminator::JumpI;
using Terminator::Return;
using Terminator::Revert;
using Terminator::SelfDestruct;
using Terminator::Stop;

TEST(TerminatorTest, Formatter)
{
    EXPECT_EQ(std::format("{}", JumpDest), "JumpDest");
    EXPECT_EQ(std::format("{}", JumpI), "JumpI");
    EXPECT_EQ(std::format("{}", Jump), "Jump");
    EXPECT_EQ(std::format("{}", Return), "Return");
    EXPECT_EQ(std::format("{}", Revert), "Revert");
    EXPECT_EQ(std::format("{}", SelfDestruct), "SelfDestruct");
    EXPECT_EQ(std::format("{}", Stop), "Stop");
}

TEST(InstructionTest, ToBlocks)
{
    blocks_eq({}, {}, {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({STOP}, {}, {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({0xEE}, {}, {{{{0, 0xEE, 0}}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({PUSH1}, {}, {{{{0, PUSH1, 0}}, Stop, INVALID_BLOCK_ID}});
    blocks_eq(
        {PUSH2, 0xf}, {}, {{{{0, PUSH2, 0xf00}}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({STOP, ADD}, {}, {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({JUMPDEST, STOP}, {{0, 0}}, {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq({ADD, REVERT}, {}, {{{{0, ADD, 0}}, Revert, INVALID_BLOCK_ID}});
    blocks_eq(
        {ADD, ADD, RETURN},
        {},
        {{{{0, ADD, 0}, {1, ADD, 0}}, Return, INVALID_BLOCK_ID}});
    blocks_eq(
        {JUMPDEST, ADD, REVERT},
        {{0, 0}},
        {{{{1, ADD, 0}}, Revert, INVALID_BLOCK_ID}});
    blocks_eq({JUMPI}, {}, {{{}, JumpI, 1}, {{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq(
        {JUMPDEST, JUMPDEST}, {{0, 0}, {1, 0}}, {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq(
        {JUMPDEST, JUMPDEST, JUMPDEST},
        {{0, 0}, {1, 0}, {2, 0}},
        {{{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq(
        {JUMPDEST, ADD, JUMPDEST},
        {{0, 0}, {2, 1}},
        {{{{1, ADD, 0}}, JumpDest, 1}, {{}, Stop, INVALID_BLOCK_ID}});
    blocks_eq(
        {ADD, ADD, JUMP, ADD, JUMPDEST, SELFDESTRUCT},
        {{4, 1}},
        {{{{0, ADD, 0}, {1, ADD, 0}}, Jump, INVALID_BLOCK_ID},
         {{}, SelfDestruct, INVALID_BLOCK_ID}});
    blocks_eq(
        {ADD, ADD, JUMP, ADD, JUMPDEST, JUMPDEST, SELFDESTRUCT},
        {{4, 1}, {5, 1}},
        {{{{0, ADD, 0}, {1, ADD, 0}}, Jump, INVALID_BLOCK_ID},
         {{}, SelfDestruct, INVALID_BLOCK_ID}});
}

TEST(BlockTest, Formatter)
{
    EXPECT_EQ(
        std::format("{}", Block{{}, Return, INVALID_BLOCK_ID}), "    Return\n");
    EXPECT_EQ(
        std::format(
            "{}",
            Block{{{0, ADD, 0}, {1, ADD, 0}}, SelfDestruct, INVALID_BLOCK_ID}),
        "      (0, ADD, 0x0)\n      (1, ADD, 0x0)\n    SelfDestruct\n");
    EXPECT_EQ(
        std::format("{}", Block{{{1, ADD, 0}}, JumpI, 0}),
        "      (1, ADD, 0x0)\n    JumpI 0\n");
}

auto const instrIR0 = InstructionIR(BytecodeIR({}));
auto const instrIR1 = InstructionIR(BytecodeIR({JUMPDEST, SUB, SUB, JUMPDEST}));
auto const instrIR2 =
    InstructionIR(BytecodeIR({JUMPDEST, JUMPDEST, SUB, JUMPDEST}));

TEST(InstructionIRTest, Formatter)
{
    EXPECT_EQ(
        std::format("{}", instrIR0),
        "instruction:\n  block 0:\n    Stop\n\n  jumpdests:\n");
    EXPECT_EQ(
        std::format("{}", instrIR1),
        "instruction:\n  block 0:\n      (1, SUB, 0x0)\n      (2, SUB, 0x0)\n  "
        "  JumpDest 1\n  block 1:\n    Stop\n\n  jumpdests:\n    3:1\n    "
        "0:0\n");
    EXPECT_EQ(
        std::format("{}", instrIR2),
        "instruction:\n  block 0:\n      (2, SUB, 0x0)\n    JumpDest 1\n  "
        "block 1:\n    Stop\n\n  jumpdests:\n    3:1\n    1:0\n    0:0\n");
}

TEST(RegistersValueTest, Formatter)
{
    EXPECT_EQ(
        std::format("{}", registers::Value{registers::ValueIs::LITERAL, 0x42}),
        "0x42");
    EXPECT_EQ(
        std::format("{}", registers::Value{registers::ValueIs::PARAM_ID, 42}),
        "%p42");
    EXPECT_EQ(
        std::format(
            "{}", registers::Value{registers::ValueIs::REGISTER_ID, 42}),
        "%r42");
}

TEST(RegistersInstrTest, Formatter)
{
    EXPECT_EQ(
        std::format(
            "{}", registers::Instr{registers::NO_REGISTER_ID, {0, POP, 0}, {}}),
        "POP [ ]");
    EXPECT_EQ(
        std::format(
            "{}",
            registers::Instr{
                1,
                {0, SUB, 0},
                {registers::Value{registers::ValueIs::LITERAL, 0},
                 registers::Value{registers::ValueIs::PARAM_ID, 0}}}),
        "%r1 = SUB [ 0x0 %p0 ]");
}

TEST(RegistersBlock, Formatter)
{
    registers::Block blk = {0, {}, {}, Stop, INVALID_BLOCK_ID};

    EXPECT_EQ(
        std::format("{}", blk),
        "    min_params: 0\n    Stop\n    output: [ ]\n");

    registers::Block blk1 = {
        1,
        {registers::Instr{
            1,
            {0, SUB, 0},
            {registers::Value{registers::ValueIs::LITERAL, 0},
             registers::Value{registers::ValueIs::PARAM_ID, 0}}}},
        {registers::Value{registers::ValueIs::REGISTER_ID, 1}},
        Stop,
        INVALID_BLOCK_ID};
    EXPECT_EQ(
        std::format("{}", blk1),
        "    min_params: 1\n      %r1 = SUB [ 0x0 %p0 ]\n    Stop\n    output: "
        "[ %r1 ]\n");

    registers::Block blk2 = {
        2,
        {registers::Instr{
             1,
             {0, SUB, 0},
             {registers::Value{registers::ValueIs::LITERAL, 0},
              registers::Value{registers::ValueIs::PARAM_ID, 0}}},
         registers::Instr{
             registers::NO_REGISTER_ID,
             {0, PUSH3, 0},
             {registers::Value{registers::ValueIs::LITERAL, 0xab}}},
         registers::Instr{2, {0, PC, 0}, {}}

        },
        {registers::Value{registers::ValueIs::LITERAL, 0x42},
         registers::Value{registers::ValueIs::PARAM_ID, 0},
         registers::Value{registers::ValueIs::REGISTER_ID, 1}},
        Stop,
        INVALID_BLOCK_ID};
    EXPECT_EQ(
        std::format("{}", blk2),
        "    min_params: 2\n      %r1 = SUB [ 0x0 %p0 ]\n      PUSH3 [ 0xab "
        "]\n      %r2 = PC [ ]\n    Stop\n    output: [ 0x42 %p0 %r1 ]\n");
}

TEST(RegistersIR, Formatter)
{
    EXPECT_EQ(
        std::format("{}", registers::RegistersIR(instrIR0)),
        "registers:\n"
        "  block 0:\n"
        "    min_params: 0\n"
        "    Stop\n"
        "    output: [ ]\n"
        "\n"
        "  jumpdests:\n");
    EXPECT_EQ(
        std::format("{}", registers::RegistersIR(instrIR1)),
        "registers:\n"
        "  block 0:\n"
        "    min_params: 3\n"
        "      %r1 = SUB [ %p0 %p1 ]\n"
        "      %r2 = SUB [ %r1 %p2 ]\n"
        "    JumpDest 1\n"
        "    output: [ %r2 ]\n"
        "  block 1:\n"
        "    min_params: 0\n"
        "    Stop\n"
        "    output: [ ]\n"
        "\n"
        "  jumpdests:\n"
        "    3:1\n"
        "    0:0\n"

    );
    EXPECT_EQ(
        std::format("{}", registers::RegistersIR(instrIR2)),
        "registers:\n"
        "  block 0:\n"
        "    min_params: 2\n"
        "      %r2 = SUB [ %p0 %p1 ]\n"
        "    JumpDest 1\n"
        "    output: [ %r2 ]\n"
        "  block 1:\n"
        "    min_params: 0\n"
        "    Stop\n"
        "    output: [ ]\n"
        "\n"
        "  jumpdests:\n"
        "    3:1\n"
        "    1:0\n"
        "    0:0\n");
}
