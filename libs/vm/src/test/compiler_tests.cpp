#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <intx/intx.hpp>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace monad::compiler;
using namespace monad::compiler::bytecode;
using namespace monad::compiler::basic_blocks;
using namespace intx;

void tokens_eq(
    std::vector<uint8_t> const &in, std::vector<Instruction> const &expected)
{
    EXPECT_EQ(BytecodeIR(in).instructions, expected);
}

TEST(TokenTest, Formatter)
{
    EXPECT_EQ(
        std::format("{}", Instruction{4, PUSH1, 0x42}), "(4, PUSH1, 0x42)");
    EXPECT_EQ(
        std::format(
            "{}",
            Instruction{
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
    BasicBlocksIR const actual(actual_bc);

    EXPECT_EQ(actual.jump_dests(), expected_jumpdests);
    EXPECT_EQ(actual.blocks(), expected_blocks);
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

TEST(BasicBlocksTest, ToBlocks)
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

auto const instrIR0 = BasicBlocksIR(BytecodeIR({}));
auto const instrIR1 = BasicBlocksIR(BytecodeIR({JUMPDEST, SUB, SUB, JUMPDEST}));
auto const instrIR2 =
    BasicBlocksIR(BytecodeIR({JUMPDEST, JUMPDEST, SUB, JUMPDEST}));

TEST(BasicBlocksIRTest, Validation)
{
    EXPECT_TRUE(instrIR0.is_valid());
    EXPECT_TRUE(instrIR1.is_valid());
    EXPECT_TRUE(instrIR2.is_valid());
}

TEST(BasicBlocksIRTest, Formatter)
{
    EXPECT_EQ(
        std::format("{}", instrIR0),
        "basic_blocks:\n  block 0:\n    Stop\n\n  jumpdests:\n");
    EXPECT_EQ(
        std::format("{}", instrIR1),
        "basic_blocks:\n  block 0:\n      (1, SUB, 0x0)\n      (2, SUB, 0x0)\n "
        " "
        "  JumpDest 1\n  block 1:\n    Stop\n\n  jumpdests:\n    3:1\n    "
        "0:0\n");
    EXPECT_EQ(
        std::format("{}", instrIR2),
        "basic_blocks:\n  block 0:\n      (2, SUB, 0x0)\n    JumpDest 1\n  "
        "block 1:\n    Stop\n\n  jumpdests:\n    3:1\n    1:0\n    0:0\n");
}

local_stacks::Value lit(uint256_t x)
{
    return local_stacks::Value{local_stacks::ValueIs::LITERAL, x};
}

local_stacks::Value param_id(uint256_t x)
{
    return local_stacks::Value{local_stacks::ValueIs::PARAM_ID, x};
}

local_stacks::Value computed()
{
    return local_stacks::Value{local_stacks::ValueIs::COMPUTED, 0};
}

TEST(LocalStacksValueTest, Formatter)
{
    EXPECT_EQ(std::format("{}", lit(0x42)), "0x42");
    EXPECT_EQ(std::format("{}", param_id(42)), "%p42");
    EXPECT_EQ(std::format("{}", computed()), "COMPUTED");
}

TEST(LocalStacksBlock, Formatter)
{
    local_stacks::Block blk = {0, {}, {}, Stop, INVALID_BLOCK_ID};

    EXPECT_EQ(
        std::format("{}", blk),
        R"(    min_params: 0
    Stop
    output: [ ]
)");

    local_stacks::Block blk1 = {1, {computed()}, {}, Stop, INVALID_BLOCK_ID};

    EXPECT_EQ(
        std::format("{}", blk1),
        R"(    min_params: 1
    Stop
    output: [ COMPUTED ]
)");

    local_stacks::Block blk2 = {
        2, {computed(), param_id(0), lit(0x42)}, {}, Stop, INVALID_BLOCK_ID};
    EXPECT_EQ(
        std::format("{}", blk2),
        R"(    min_params: 2
    Stop
    output: [ COMPUTED %p0 0x42 ]
)");
}

TEST(LocalStacksIR, Formatter)
{
    EXPECT_EQ(
        std::format("{}", local_stacks::LocalStacksIR(std::move(instrIR0))),
        R"(local_stacks:
  block 0:
    min_params: 0
    Stop
    output: [ ]

  jumpdests:
)");

    EXPECT_EQ(
        std::format("{}", local_stacks::LocalStacksIR(std::move(instrIR1))),
        R"(local_stacks:
  block 0:
    min_params: 3
      (1, SUB, 0x0)
      (2, SUB, 0x0)
    JumpDest 1
    output: [ COMPUTED ]
  block 1:
    min_params: 0
    Stop
    output: [ ]

  jumpdests:
    3:1
    0:0
)");

    EXPECT_EQ(
        std::format("{}", local_stacks::LocalStacksIR(std::move(instrIR2))),
        R"(local_stacks:
  block 0:
    min_params: 2
      (2, SUB, 0x0)
    JumpDest 1
    output: [ COMPUTED ]
  block 1:
    min_params: 0
    Stop
    output: [ ]

  jumpdests:
    3:1
    1:0
    0:0
)");

    EXPECT_EQ(
        std::format(
            "{}",
            local_stacks::LocalStacksIR(BasicBlocksIR(BytecodeIR(
                {PUSH0,
                 PUSH1,
                 0xa,
                 PC,
                 PC,
                 ADD,
                 PC,
                 DUP1,
                 DUP3,
                 SWAP1,
                 POP,
                 SWAP4,
                 DUP6,
                 SWAP7})))),
        R"(local_stacks:
  block 0:
    min_params: 2
      (0, PUSH0, 0x0)
      (1, PUSH1, 0xa)
      (3, PC, 0x0)
      (4, PC, 0x0)
      (5, ADD, 0x0)
      (6, PC, 0x0)
      (7, DUP1, 0x0)
      (8, DUP3, 0x0)
      (9, SWAP1, 0x0)
      (10, POP, 0x0)
      (11, SWAP4, 0x0)
      (12, DUP6, 0x0)
      (13, SWAP7, 0x0)
    Stop
    output: [ %p1 0x0 0x6 COMPUTED 0xa COMPUTED %p0 %p0 ]

  jumpdests:
)");
}
