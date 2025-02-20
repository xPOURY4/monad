#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/fuzzing/generator/generator.hpp>

#include <gtest/gtest.h>

#include <format>
#include <iostream>

using namespace monad::fuzzing;
using namespace monad::compiler::basic_blocks;

using namespace evmc::literals;

TEST(FuzzTest, Demo)
{
    auto rd = std::random_device();
    auto eng = std::mt19937_64(rd());

    auto p = generate_program(
        eng, {0x0000000000000000000000000000000000001234_address});
    auto bb = BasicBlocksIR(p);

    std::cout << std::format("{}\n", bb);

    for (auto b : p) {
        std::cout << std::format("{:02X}", b);
    }
    std::cout << '\n';
}
