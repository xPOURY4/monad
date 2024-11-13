#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/x86/virtual_stack.h>
#include <compiler/types.h>

#include <evmc/evmc.hpp>

#include <gtest/gtest.h>

#include <cassert>
#include <cstdint>
#include <utility>

using namespace monad::compiler;
using namespace monad::compiler::basic_blocks;
using namespace monad::compiler::local_stacks;
using namespace monad::compiler::stack;

using enum InstructionCode;
using enum Terminator;

// Helpers for constructing test inputs

constexpr Instruction inst(InstructionCode op)
{
    return Instruction{
        .offset = 0,
        .code = op,
        .index = 0,
        .operand = 0,
    };
}

constexpr Instruction swap(uint8_t index)
{
    assert(index > 0);
    return Instruction{
        .offset = 0,
        .code = Swap,
        .index = index,
        .operand = 0,
    };
}

constexpr Instruction dup_(uint8_t index)
{
    assert(index > 0);
    return Instruction{
        .offset = 0,
        .code = Dup,
        .index = index,
        .operand = 0,
    };
}

constexpr Instruction push(uint256_t val)
{
    return Instruction{
        .offset = 0,
        .code = Push,
        .index = 0,
        .operand = val,
    };
}

template <typename T>
constexpr Instruction make_inst(T elem)
    requires(
        std::is_same_v<T, Instruction> || std::is_same_v<T, InstructionCode>)
{
    if constexpr (std::is_same_v<T, Instruction>) {
        return elem;
    }
    else {
        return inst(elem);
    }
}

template <typename... Ts>
constexpr auto block(Ts... insts)
{
    return [=](Terminator t = Terminator::Stop) {
        return convert_block(
            {
                .instrs = {make_inst(insts)...},
                .terminator = t,
            },
            0);
    };
}

// Tests

TEST(VirtualStack, PositiveDelta)
{
    auto insts = block(push(0), push(0), Pop, push(0), push(0), SStore)();
    auto s = Stack(insts);

    ASSERT_EQ(s.min_delta(), 0);
    ASSERT_EQ(s.max_delta(), 3);
    ASSERT_EQ(s.delta(), 1);
}

TEST(VirtualStack, NegativeDelta)
{
    auto insts = block(Pop, Pop, Call, push(0), Pop)();
    auto s = Stack(insts);

    ASSERT_EQ(s.min_delta(), -9);
    ASSERT_EQ(s.max_delta(), 0);
    ASSERT_EQ(s.delta(), -8);
}

TEST(VirtualStack, ControlFlowDelta)
{
    auto insts = block(push(0), push(0), Add)(Jump);
    auto s = Stack(insts);

    ASSERT_EQ(s.min_delta(), 0);
    ASSERT_EQ(s.max_delta(), 2);
    ASSERT_EQ(s.delta(), 0);
}

TEST(VirtualStack, SwapDelta)
{
    {
        auto insts = block(swap(1))();
        auto s = Stack(insts);

        ASSERT_EQ(s.min_delta(), -2);
        ASSERT_EQ(s.max_delta(), 0);
        ASSERT_EQ(s.delta(), 0);
    }

    {
        auto insts = block(push(0), push(0), swap(10), Pop)(Jump);
        auto s = Stack(insts);

        ASSERT_EQ(s.min_delta(), -9);
        ASSERT_EQ(s.max_delta(), 2);
        ASSERT_EQ(s.delta(), 0);
    }
}

TEST(VirtualStack, DupDelta)
{
    {
        auto insts = block(dup_(1))();
        auto s = Stack(insts);

        ASSERT_EQ(s.min_delta(), -1);
        ASSERT_EQ(s.max_delta(), 1);
        ASSERT_EQ(s.delta(), 1);
    }

    {
        auto insts = block(dup_(7))();
        auto s = Stack(insts);

        ASSERT_EQ(s.min_delta(), -7);
        ASSERT_EQ(s.max_delta(), 1);
        ASSERT_EQ(s.delta(), 1);
    }
}

TEST(VirtualStack, PushPop)
{
    auto insts = block(push(0), push(0), push(0))();
    auto s = Stack(insts);

    auto lit = Literal(evmc::bytes32(100));
    auto avx = AvxRegister(3);
    auto mem = StackOffset(0);

    ASSERT_TRUE(s.empty());

    s.push(lit);
    ASSERT_EQ(s.top(), StackElement(lit));
    ASSERT_EQ(s.size(), 1);

    s.push(avx);
    ASSERT_EQ(s.top(), StackElement(avx));
    ASSERT_EQ(s.size(), 2);

    s.push(mem);
    ASSERT_EQ(s.top(), StackElement(mem));
    ASSERT_EQ(s.size(), 3);

    ASSERT_EQ(s.index(0), StackElement(lit));
    ASSERT_EQ(s.index(1), StackElement(avx));
    ASSERT_EQ(s.index(2), StackElement(mem));

    s.pop();
    ASSERT_EQ(s.top(), StackElement(avx));

    s.pop();
    ASSERT_EQ(s.top(), StackElement(lit));

    s.pop();
    ASSERT_TRUE(s.empty());
}

TEST(VirtualStack, NegativeElements)
{
    auto insts = block(Call)();
    auto s = Stack(insts);

    // The stack gets initialized with some elements because the basic block
    // doesn't push enough for the CALL instruction
    ASSERT_FALSE(s.empty());
    ASSERT_EQ(s.size(), 7);

    // The input (negative) elements are all stack offsets
    for (auto i = -7; i < 0; ++i) {
        ASSERT_EQ(s.index(i), StackElement(StackOffset(i)));
    }
}

TEST(VirtualStack, Swaps)
{
    auto insts = block(push(0), push(0), push(0))();
    auto s = Stack(insts);

    s.push(Literal(evmc::bytes32(0)));
    s.push(Literal(evmc::bytes32(1)));
    s.push(Literal(evmc::bytes32(2)));

    s.swap(1);
    ASSERT_EQ(s.index(0), StackElement(Literal(evmc::bytes32(0))));
    ASSERT_EQ(s.index(1), StackElement(Literal(evmc::bytes32(2))));
    ASSERT_EQ(s.index(2), StackElement(Literal(evmc::bytes32(1))));

    s.swap(0);
    ASSERT_EQ(s.index(0), StackElement(Literal(evmc::bytes32(1))));
    ASSERT_EQ(s.index(1), StackElement(Literal(evmc::bytes32(2))));
    ASSERT_EQ(s.index(2), StackElement(Literal(evmc::bytes32(0))));
}

TEST(VirtualStack, Dups)
{
    auto insts = block(push(0), push(0), push(0))();
    auto s = Stack(insts);

    s.push(AvxRegister(0));
    s.push(Literal(evmc::bytes32(1)));
    ASSERT_EQ(s.size(), 2);

    s.dup(0);
    ASSERT_EQ(s.size(), 3);
    ASSERT_EQ(s.index_operand(0), std::pair(Operand(AvxRegister(0)), false));
    ASSERT_EQ(
        s.index_operand(1),
        std::pair(Operand(Literal(evmc::bytes32(1))), false));
    ASSERT_EQ(s.index_operand(2), std::pair(Operand(AvxRegister(0)), false));

    // When we've popped the duplicated item off the stack, index_operand tells
    // us that the original value is safe to deallocate on being popped
    s.pop();
    ASSERT_EQ(s.size(), 2);
    ASSERT_EQ(s.index_operand(0), std::pair(Operand(AvxRegister(0)), true));
    ASSERT_EQ(
        s.index_operand(1),
        std::pair(Operand(Literal(evmc::bytes32(1))), false));
}
