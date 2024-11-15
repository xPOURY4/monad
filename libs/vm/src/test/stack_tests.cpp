#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/bytecode.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/opcodes.h"
#include <compiler/ir/x86/virtual_stack.h>

#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace monad::compiler;
using namespace monad::compiler::stack;

namespace
{
    struct StackElemTestData
    {
        explicit StackElemTestData(std::set<std::int64_t> stack_indices)
            : stack_indices{std::move(stack_indices)}
        {
        }

        StackElemTestData &with_stack_offset(StackOffset x)
        {
            stack_offset = x;
            return *this;
        }

        StackElemTestData &with_avx_reg(AvxReg x)
        {
            avx_reg = x;
            return *this;
        }

        StackElemTestData &with_general_reg(GeneralReg x)
        {
            general_reg = x;
            return *this;
        }

        StackElemTestData &with_literal(Literal x)
        {
            literal = x;
            return *this;
        }

        std::optional<StackOffset> stack_offset;
        std::optional<AvxReg> avx_reg;
        std::optional<GeneralReg> general_reg;
        std::optional<Literal> literal;
        std::set<std::int64_t> stack_indices;
    };

    bool test_stack_element(StackElemRef e, StackElemTestData t)
    {
        return e->stack_offset() == t.stack_offset &&
               e->avx_reg() == t.avx_reg && e->general_reg() == t.general_reg &&
               e->literal() == t.literal &&
               e->stack_indices() == t.stack_indices;
    }
}

TEST(VirtualStack, ctor_test_1)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({ADD})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -2);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), -1);
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{-2}}.with_stack_offset({-2})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
}

TEST(VirtualStack, ctor_test_2)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({ADD, SSTORE, JUMP})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -4);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), -4);
    ASSERT_TRUE(test_stack_element(
        stack.get(-3), StackElemTestData{{-3}}.with_stack_offset({-3})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{-2}}.with_stack_offset({-2})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
}

TEST(VirtualStack, ctor_test_3)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR({PUSH0, PUSH1, 0, ADD, PUSH2, 0, 0, JUMPI})));
    Stack const stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), 0);
    ASSERT_EQ(stack.max_delta(), 2);
    ASSERT_EQ(stack.delta(), 0);
}

TEST(VirtualStack, ctor_test_4)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({SWAP1})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -2);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), 0);
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{-2}}.with_stack_offset({-2})));
}

TEST(VirtualStack, ctor_test_5)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({SWAP16})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -17);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), 0);
    for (int64_t i = -1; i >= -17; --i) {
        ASSERT_TRUE(test_stack_element(
            stack.get(i), StackElemTestData{{i}}.with_stack_offset({i})));
    }
}

TEST(VirtualStack, ctor_test_6)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({DUP1})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -1);
    ASSERT_EQ(stack.max_delta(), 1);
    ASSERT_EQ(stack.delta(), 1);
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
}

TEST(VirtualStack, ctor_test_7)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({DUP16})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -16);
    ASSERT_EQ(stack.max_delta(), 1);
    ASSERT_EQ(stack.delta(), 1);
    for (int64_t i = -1; i >= -16; --i) {
        ASSERT_TRUE(test_stack_element(
            stack.get(i), StackElemTestData{{i}}.with_stack_offset({i})));
    }
}

TEST(VirtualStack, ctor_test_8)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(
            {PUSH0, ADD, ISZERO, DUP1, SWAP2, SWAP1, PUSH0, PUSH0, REVERT})));
    Stack stack{ir.blocks[0]};
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -2);
    ASSERT_EQ(stack.max_delta(), 3);
    ASSERT_EQ(stack.delta(), 1);
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{-2}}.with_stack_offset({-2})));
}

TEST(VirtualStack, push_test)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({PUSH1, 1})));
    Stack stack{ir.blocks[0]};
    stack.push_literal(1);
    ASSERT_EQ(stack.top_index(), 0);
    ASSERT_EQ(stack.min_delta(), 0);
    ASSERT_EQ(stack.max_delta(), 1);
    ASSERT_EQ(stack.delta(), 1);
    ASSERT_TRUE(test_stack_element(
        stack.get(0), StackElemTestData{{0}}.with_literal({1})));
}

TEST(VirtualStack, pop_test)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({NOT})));
    Stack stack{ir.blocks[0]};
    auto e = stack.pop();
    ASSERT_EQ(stack.top_index(), -2);
    ASSERT_EQ(stack.min_delta(), -1);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), 0);
    ASSERT_TRUE(
        test_stack_element(e, StackElemTestData{{}}.with_stack_offset({-1})));
}

TEST(VirtualStack, swap_test)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({SWAP2})));
    Stack stack{ir.blocks[0]};
    stack.swap(-3);
    ASSERT_EQ(stack.top_index(), -1);
    ASSERT_EQ(stack.min_delta(), -3);
    ASSERT_EQ(stack.max_delta(), 0);
    ASSERT_EQ(stack.delta(), 0);
    ASSERT_TRUE(test_stack_element(
        stack.get(-3), StackElemTestData{{-3}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{-2}}.with_stack_offset({-2})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-3})));
}

TEST(VirtualStack, dup_test)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({DUP2})));
    Stack stack{ir.blocks[0]};
    stack.dup(-2);
    ASSERT_EQ(stack.top_index(), 0);
    ASSERT_EQ(stack.min_delta(), -2);
    ASSERT_EQ(stack.max_delta(), 1);
    ASSERT_EQ(stack.delta(), 1);
    ASSERT_TRUE(test_stack_element(
        stack.get(-2), StackElemTestData{{0, -2}}.with_stack_offset({-2})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(0), StackElemTestData{{0, -2}}.with_stack_offset({-2})));
}

TEST(VirtualStack, push_pop_dup_swap_test_1)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR({PUSH0, DUP2, DUP2, POP, SWAP1})));
    Stack stack{ir.blocks[0]};
    stack.push_literal(0);
    stack.dup(-1);
    stack.dup(0);
    auto e = stack.pop();
    stack.swap(0);
    ASSERT_EQ(stack.top_index(), 1);
    ASSERT_EQ(stack.min_delta(), -1);
    ASSERT_EQ(stack.max_delta(), 3);
    ASSERT_EQ(stack.delta(), 2);
    ASSERT_TRUE(
        test_stack_element(e, StackElemTestData{{1}}.with_literal({0})));
    ASSERT_TRUE(test_stack_element(
        stack.get(-1), StackElemTestData{{-1, 0}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(0), StackElemTestData{{-1, 0}}.with_stack_offset({-1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(1), StackElemTestData{{1}}.with_literal({0})));
}

TEST(VirtualStack, insert_stack_offset_test_1)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({PUSH0})));
    Stack stack{ir.blocks[0]};
    stack.push_literal(0);
    stack.insert_stack_offset(stack.get(0));
    ASSERT_TRUE(test_stack_element(
        stack.get(0),
        StackElemTestData{{0}}.with_literal({0}).with_stack_offset({0})));
}

TEST(VirtualStack, insert_stack_offset_test_2)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR({PUSH0, PUSH0, PUSH0})));
    Stack stack{ir.blocks[0]};
    stack.push_literal(0);
    stack.push_literal(0);
    stack.push_literal(0);
    stack.insert_stack_offset(stack.get(0), 1);
    stack.insert_stack_offset(stack.get(1));
    stack.insert_stack_offset(stack.get(2));
    ASSERT_TRUE(test_stack_element(
        stack.get(0),
        StackElemTestData{{0}}.with_literal({0}).with_stack_offset({1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(1),
        StackElemTestData{{1}}.with_literal({0}).with_stack_offset({0})));
    ASSERT_TRUE(test_stack_element(
        stack.get(2),
        StackElemTestData{{2}}.with_literal({0}).with_stack_offset({2})));
}

TEST(VirtualStack, insert_stack_offset_test_3)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR({PUSH0, PUSH0, PUSH0})));
    Stack stack{ir.blocks[0]};
    stack.push_literal(0);
    stack.push_literal(0);
    stack.push_literal(0);
    stack.insert_stack_offset(stack.get(0), 1);
    stack.insert_stack_offset(stack.get(2));
    stack.insert_stack_offset(stack.get(1));
    ASSERT_TRUE(test_stack_element(
        stack.get(0),
        StackElemTestData{{0}}.with_literal({0}).with_stack_offset({1})));
    ASSERT_TRUE(test_stack_element(
        stack.get(1),
        StackElemTestData{{1}}.with_literal({0}).with_stack_offset({0})));
    ASSERT_TRUE(test_stack_element(
        stack.get(2),
        StackElemTestData{{2}}.with_literal({0}).with_stack_offset({2})));
}

TEST(VirtualStack, alloc_stack_offset_test_1)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR({POP, POP})));
    Stack stack{ir.blocks[0]};
    stack.pop();
    stack.pop();
    auto e1 = stack.alloc_stack_offset(-2);
    auto e2 = stack.alloc_stack_offset(-2);
    ASSERT_TRUE(
        test_stack_element(e1, StackElemTestData{{}}.with_stack_offset({-2})));
    ASSERT_TRUE(
        test_stack_element(e2, StackElemTestData{{}}.with_stack_offset({-1})));
    stack.push(e1);
    stack.push(e2);
    ASSERT_TRUE(test_stack_element(
        e1, StackElemTestData{{-2}}.with_stack_offset({-2})));
    ASSERT_TRUE(test_stack_element(
        e2, StackElemTestData{{-1}}.with_stack_offset({-1})));
}

TEST(VirtualStack, insert_avx_reg_test_1)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR(std::vector<uint8_t>(AvxRegCount + 1, POP))));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        stack.insert_avx_reg(stack.get(-i - 1));
    }
    auto p = stack.insert_avx_reg(stack.get(-AvxRegCount - 1));
    ASSERT_FALSE(p.second.has_value());
    std::uint8_t rem_ix = AvxRegCount;
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        if (!stack.get(-i - 1)->avx_reg().has_value()) {
            ASSERT_TRUE(rem_ix == AvxRegCount);
            rem_ix = i;
        }
    }
    ASSERT_LT(rem_ix, AvxRegCount);
    ASSERT_EQ(stack.get(-AvxRegCount - 1)->avx_reg().value().reg, rem_ix);
}

TEST(VirtualStack, insert_general_reg_test_1)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        bytecode::BytecodeIR(std::vector<uint8_t>(GeneralRegCount + 1, POP))));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        stack.insert_general_reg(stack.get(-i - 1));
    }
    auto p = stack.insert_general_reg(stack.get(-GeneralRegCount - 1));
    ASSERT_FALSE(p.second.has_value());
    std::uint8_t rem_ix = GeneralRegCount;
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        if (!stack.get(-i - 1)->general_reg().has_value()) {
            ASSERT_TRUE(rem_ix == GeneralRegCount);
            rem_ix = i;
        }
    }
    ASSERT_LT(rem_ix, GeneralRegCount);
    ASSERT_EQ(
        stack.get(-GeneralRegCount - 1)->general_reg().value().reg, rem_ix);
}

TEST(VirtualStack, insert_avx_reg_test_2)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i <= AvxRegCount; ++i) {
        bytecode.push_back(POP);
    }
    for (std::uint8_t i = 0; i <= AvxRegCount; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i <= AvxRegCount; ++i) {
        stack.pop();
    }
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        auto p = stack.alloc_avx_reg();
        ASSERT_EQ(std::get<0>(p)->avx_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    stack.push_literal(0);
    auto p = stack.insert_avx_reg(stack.get(-1));
    ASSERT_TRUE(p.second.has_value());
    std::uint8_t rem_ix = AvxRegCount;
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        if (!stack.get(-AvxRegCount - 1 + i)->avx_reg().has_value()) {
            ASSERT_TRUE(rem_ix == AvxRegCount);
            ASSERT_TRUE(
                stack.get(-AvxRegCount - 1 + i)->stack_offset().has_value());
            rem_ix = i;
        }
    }
    ASSERT_LT(rem_ix, AvxRegCount);
    ASSERT_EQ(stack.get(-1)->avx_reg().value().reg, rem_ix);
}

TEST(VirtualStack, insert_general_reg_test_2)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i <= GeneralRegCount; ++i) {
        bytecode.push_back(POP);
    }
    for (std::uint8_t i = 0; i <= GeneralRegCount; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i <= GeneralRegCount; ++i) {
        stack.pop();
    }
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        auto p = stack.alloc_general_reg();
        ASSERT_EQ(std::get<0>(p)->general_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    stack.push_literal(0);
    auto p = stack.insert_general_reg(stack.get(-1));
    ASSERT_TRUE(p.second.has_value());
    std::uint8_t rem_ix = GeneralRegCount;
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        if (!stack.get(-GeneralRegCount - 1 + i)->general_reg().has_value()) {
            ASSERT_TRUE(rem_ix == GeneralRegCount);
            ASSERT_TRUE(stack.get(-GeneralRegCount - 1 + i)
                            ->stack_offset()
                            .has_value());
            rem_ix = i;
        }
    }
    ASSERT_LT(rem_ix, GeneralRegCount);
    ASSERT_EQ(stack.get(-1)->general_reg().value().reg, rem_ix);
}

TEST(VirtualStack, insert_avx_reg_test_3)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < AvxRegCount + 3; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    std::vector<AvxRegReserv> reservs;
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        auto p = stack.alloc_avx_reg();
        ASSERT_EQ(std::get<0>(p)->avx_reg().value().reg, i);
        stack.push(std::get<0>(p));
        reservs.push_back(std::get<1>(p));
    }
    {
        stack.push_literal(0);
        reservs.pop_back();
        auto p = stack.insert_avx_reg(stack.get(AvxRegCount));
        ASSERT_EQ(
            stack.get(AvxRegCount)->avx_reg().value().reg, AvxRegCount - 1);
        reservs.push_back(p.first);
    }
    {
        stack.push_literal(0);
        std::swap(reservs[AvxRegCount / 2], reservs.back());
        reservs.pop_back();
        auto p = stack.insert_avx_reg(stack.get(AvxRegCount + 1));
        ASSERT_EQ(
            stack.get(AvxRegCount + 1)->avx_reg().value().reg, AvxRegCount / 2);
        reservs.push_back(p.first);
    }
    {
        stack.push_literal(0);
        std::swap(reservs[0], reservs.back());
        reservs.pop_back();
        auto p = stack.insert_avx_reg(stack.get(AvxRegCount + 2));
        ASSERT_EQ(stack.get(AvxRegCount + 2)->avx_reg().value().reg, 0);
        reservs.push_back(p.first);
    }
}

TEST(VirtualStack, insert_general_reg_test_3)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < GeneralRegCount + 3; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    std::vector<GeneralRegReserv> reservs;
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        auto p = stack.alloc_general_reg();
        ASSERT_EQ(std::get<0>(p)->general_reg().value().reg, i);
        stack.push(std::get<0>(p));
        reservs.push_back(std::get<1>(p));
    }
    {
        stack.push_literal(0);
        reservs.pop_back();
        auto p = stack.insert_general_reg(stack.get(GeneralRegCount));
        ASSERT_EQ(
            stack.get(GeneralRegCount)->general_reg().value().reg,
            GeneralRegCount - 1);
        reservs.push_back(p.first);
    }
    {
        stack.push_literal(0);
        std::swap(reservs[GeneralRegCount / 2], reservs.back());
        reservs.pop_back();
        auto p = stack.insert_general_reg(stack.get(GeneralRegCount + 1));
        ASSERT_EQ(
            stack.get(GeneralRegCount + 1)->general_reg().value().reg,
            GeneralRegCount / 2);
        reservs.push_back(p.first);
    }
    {
        stack.push_literal(0);
        std::swap(reservs[0], reservs.back());
        reservs.pop_back();
        auto p = stack.insert_general_reg(stack.get(GeneralRegCount + 2));
        ASSERT_EQ(stack.get(GeneralRegCount + 2)->general_reg().value().reg, 0);
        reservs.push_back(p.first);
    }
}

TEST(VirtualStack, spill_all_avx_regs_test_1)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        auto p = stack.alloc_avx_reg();
        ASSERT_EQ(std::get<0>(p)->avx_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    auto ups = stack.spill_all_avx_regs();
    ASSERT_EQ(ups.size(), AvxRegCount);
    for (std::uint8_t i = 0; i < AvxRegCount; ++i) {
        ASSERT_EQ(ups[i].first.reg, i);
        ASSERT_EQ(ups[i].second.offset, i);
    }
}

TEST(VirtualStack, spill_all_caller_save_general_regs_test_1)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < GeneralRegCount; ++i) {
        auto p = stack.alloc_general_reg();
        ASSERT_EQ(std::get<0>(p)->general_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    auto ups = stack.spill_all_caller_save_general_regs();
    ASSERT_EQ(ups.size(), GeneralRegCount - 1);
    for (std::uint8_t i = 0; i < GeneralRegCount - 1; ++i) {
        ASSERT_EQ(ups[i].first.reg, i + 1);
        ASSERT_EQ(ups[i].second.offset, i + 1);
    }
}

TEST(VirtualStack, spill_all_avx_regs_test_2)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < AvxRegCount - 1; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < AvxRegCount - 1; ++i) {
        auto p = stack.alloc_avx_reg();
        ASSERT_EQ(std::get<0>(p)->avx_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    auto ups = stack.spill_all_avx_regs();
    ASSERT_EQ(ups.size(), AvxRegCount - 1);
    for (std::uint8_t i = 0; i < AvxRegCount - 1; ++i) {
        ASSERT_EQ(ups[i].first.reg, i);
        ASSERT_EQ(ups[i].second.offset, i);
    }
}

TEST(VirtualStack, spill_all_caller_save_general_regs_test_2)
{
    std::vector<uint8_t> bytecode;
    for (std::uint8_t i = 0; i < GeneralRegCount - 1; ++i) {
        bytecode.push_back(PUSH0);
    }
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR(bytecode::BytecodeIR(bytecode)));
    Stack stack{ir.blocks[0]};
    for (std::uint8_t i = 0; i < GeneralRegCount - 1; ++i) {
        auto p = stack.alloc_general_reg();
        ASSERT_EQ(std::get<0>(p)->general_reg().value().reg, i);
        stack.push(std::get<0>(p));
    }
    auto ups = stack.spill_all_caller_save_general_regs();
    ASSERT_EQ(ups.size(), GeneralRegCount - 2);
    for (std::uint8_t i = 0; i < GeneralRegCount - 2; ++i) {
        ASSERT_EQ(ups[i].first.reg, i + 1);
        ASSERT_EQ(ups[i].second.offset, i + 1);
    }
}
