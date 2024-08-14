#include <cstdint>
#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

#include <compiler/ir/bytecode.h>
#include <compiler/ir/instruction.h>

using namespace monad::compiler;

void tokens_eq(
    std::vector<uint8_t> const &in, std::vector<Token> const &expected)
{
    EXPECT_EQ(BytecodeIR(in).tokens, expected);
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
    tokens_eq({PUSH32, 0xff}, {{0, PUSH32, (uint256_t)0xff << 248}});
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
