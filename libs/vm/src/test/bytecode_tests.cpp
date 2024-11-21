#include <compiler/ir/bytecode.h>
#include <compiler/opcodes.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace intx;
using namespace monad::compiler;

TEST(Bytecode, Stop)
{
    auto bc = Bytecode({STOP});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), STOP);
    ASSERT_EQ(inst.static_gas_cost(), 0);
    ASSERT_FALSE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
}

TEST(Bytecode, Add)
{
    auto bc = Bytecode({ADD});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 2);
    ASSERT_EQ(inst.opcode(), ADD);
    ASSERT_EQ(inst.static_gas_cost(), 3);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
}

TEST(Bytecode, Call)
{
    auto bc = Bytecode({CALL});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 7);
    ASSERT_EQ(inst.opcode(), CALL);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_TRUE(inst.dynamic_gas());
}

TEST(Bytecode, Dup)
{
    auto bc = Bytecode({DUP11});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_TRUE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 11);
    ASSERT_EQ(inst.opcode(), DUP11);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 11);
}

TEST(Bytecode, Swap)
{
    auto bc = Bytecode({SWAP7});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_TRUE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 8);
    ASSERT_EQ(inst.opcode(), SWAP7);
    ASSERT_FALSE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 7);
}

TEST(Bytecode, Log)
{
    auto bc = Bytecode({LOG2});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_TRUE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 4);
    ASSERT_EQ(inst.opcode(), LOG2);
    ASSERT_FALSE(inst.increases_stack());
    ASSERT_TRUE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 2);
}

TEST(Bytecode, Push0)
{
    auto bc = Bytecode<EVMC_SHANGHAI>({PUSH0});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH0);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 0);
    ASSERT_EQ(inst.immediate_value(), 0);
}

TEST(Bytecode, Push1)
{
    auto bc = Bytecode({PUSH1, 0x11});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH1);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 1);
    ASSERT_EQ(inst.immediate_value(), 0x11);
}

TEST(Bytecode, Push2)
{
    auto bc = Bytecode({PUSH2, 0x11, 0x22});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH2);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 2);
    ASSERT_EQ(inst.immediate_value(), 0x1122);
}

TEST(Bytecode, Push8)
{
    auto bc = Bytecode({PUSH8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH8);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 8);
    ASSERT_EQ(inst.immediate_value(), 0x1122334455667788);
}

TEST(Bytecode, Padding)
{
    auto bc = Bytecode({PUSH4, 0xAA, 0xBB});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH4);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 4);
    ASSERT_EQ(inst.immediate_value(), 0xAABB0000);
}

TEST(Bytecode, Push32)
{
    auto bc = Bytecode({PUSH32, 0xAB});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH32);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 32);
    ASSERT_EQ(
        inst.immediate_value(),
        0xAB00000000000000000000000000000000000000000000000000000000000000_u256);
}

TEST(Bytecode, Program)
{
    auto bc = Bytecode(
        {JUMPDEST, PUSH3, 0xFF, 0xCC, 0xAA, PUSH0, SWAP1, SSTORE, PUSH0, JUMP});

    auto i = [&](std::uint32_t pc, auto &&...args) {
        return Instruction::lookup<bc.revision>(
            pc, std::forward<decltype(args)>(args)...);
    };

    auto const &insts = bc.instructions();
    ASSERT_EQ(
        insts,
        std::vector({
            i(0, JUMPDEST),
            i(1, PUSH3, 0xFFCCAA),
            i(5, PUSH0),
            i(6, SWAP1),
            i(7, SSTORE),
            i(8, PUSH0),
            i(9, JUMP),
        }));
}

TEST(Bytecode, Push0London)
{
    auto bc = Bytecode<EVMC_LONDON>({PUSH0});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_FALSE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());
}

TEST(Bytecode, Push0Shanghai)
{
    auto bc = Bytecode<EVMC_SHANGHAI>({PUSH0});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_TRUE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 0);
    ASSERT_EQ(inst.opcode(), PUSH0);
    ASSERT_TRUE(inst.increases_stack());
    ASSERT_FALSE(inst.dynamic_gas());
    ASSERT_EQ(inst.index(), 0);
    ASSERT_EQ(inst.immediate_value(), 0);
}

TEST(Bytecode, RevertHomestead)
{
    auto bc = Bytecode<EVMC_HOMESTEAD>({REVERT});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_FALSE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());
}

TEST(Bytecode, RevertLatest)
{
    auto bc = Bytecode({REVERT});

    auto const &insts = bc.instructions();
    ASSERT_EQ(insts.size(), 1);

    auto const &inst = insts[0];
    ASSERT_TRUE(inst.is_valid());

    ASSERT_FALSE(inst.is_dup());
    ASSERT_FALSE(inst.is_swap());
    ASSERT_FALSE(inst.is_push());
    ASSERT_FALSE(inst.is_log());

    ASSERT_EQ(inst.stack_args(), 2);
    ASSERT_EQ(inst.opcode(), REVERT);
    ASSERT_FALSE(inst.increases_stack());
    ASSERT_TRUE(inst.dynamic_gas());
}
