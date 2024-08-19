#include <vm/vm.h>

#include <gtest/gtest.h>
#include <iostream>

using namespace monad::vm;

TEST(SimpleExecutionTests, Stop)
{
    auto [entry, stack_ptr, stack] = jit_compile_program("00");
    entry(nullptr, nullptr);

    ASSERT_EQ(*stack_ptr, 0);
}

TEST(SimpleExecutionTests, Push0)
{
    auto [entry, stack_ptr, stack] = jit_compile_program("5F00");
    entry(nullptr, nullptr);

    ASSERT_EQ(*stack_ptr, 1);
    ASSERT_EQ(stack[0], 0);
}

TEST(SimpleExecutionTests, Push1)
{
    auto [entry, stack_ptr, stack] = jit_compile_program("600100");
    entry(nullptr, nullptr);

    ASSERT_EQ(*stack_ptr, 1);
    ASSERT_EQ(stack[0], 1);
}

TEST(SimpleExecutionTests, MultiplePushes)
{
    auto [entry, stack_ptr, stack] =
        jit_compile_program("5F60116122226233333300");
    entry(nullptr, nullptr);

    ASSERT_EQ(*stack_ptr, 4);
    ASSERT_EQ(stack[0], 0);
    ASSERT_EQ(stack[1], 0x11);
    ASSERT_EQ(stack[2], 0x2222);
    ASSERT_EQ(stack[3], 0x333333);
}

TEST(SimpleExecutionTests, Push32)
{
    using namespace intx;

    auto [entry, stack_ptr, stack] = jit_compile_program(
        "7F323232323232323232323232323232323232323232323232323232323232323200");
    entry(nullptr, nullptr);

    ASSERT_EQ(*stack_ptr, 1);
    ASSERT_EQ(
        stack[0],
        0x3232323232323232323232323232323232323232323232323232323232323232_u256);
}
