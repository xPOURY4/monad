#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/types.h"
#include "poly_typed/infer_state.h"
#include <cstddef>
#include <iostream>
#include <poly_typed/strongly_connected_components.h>

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

using namespace monad::compiler;
using namespace monad::compiler::poly_typed;

namespace
{
    InferState make_infer_state(
        std::unordered_map<byte_offset, block_id> const &jumpdests,
        std::vector<local_stacks::Block> const &pre_blocks)
    {
        return InferState{
            .jumpdests = jumpdests,
            .pre_blocks = pre_blocks,
            .next_fresh_var_name = 0,
            .subst_maps = {},
            .block_types = {}};
    }

    void assert_components(
        std::vector<Component> const &x, std::vector<Component> const &y)
    {
        ASSERT_EQ(x.size(), y.size());
        for (size_t i = 0; i < x.size(); ++i) {
            auto const &x1 = x[i];
            auto const &y1 = y[i];
            ASSERT_EQ(x1.size(), y1.size());
            for (size_t j = 0; j < x1.size(); ++j) {
                ASSERT_EQ(x1[j], y1[j]);
            }
        }
    }

    __attribute__((unused)) void
    debug_print(std::vector<Component> const &components)
    {
        std::cout << "{";
        for (auto const &c : components) {
            std::cout << "{";
            for (auto x : c) {
                std::cout << x << ",";
            }
            std::cout << "},";
        }
        std::cout << "}\n";
    }
}

TEST(poly_typed, strongly_connected_components_1)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {};
    std::vector<local_stacks::Block> const pre_blocks = {local_stacks::Block{
        .min_params = 0,
        .output = {},
        .instrs = {},
        .terminator = basic_blocks::Terminator::Stop,
        .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{0}});
}

TEST(poly_typed, strongly_connected_components_2)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {
        {1, 1}, {2, 2}};
    std::vector<local_stacks::Block> const pre_blocks = {
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 1}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 2}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 1}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{2, 1}, {0}});
}

TEST(poly_typed, strongly_connected_components_3)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {
        {0, 0}, {1, 1}, {2, 2}, {3, 3}};
    std::vector<local_stacks::Block> const pre_blocks = {
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 2}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 1},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 3}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 2}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{3, 2}, {1, 0}});
}

TEST(poly_typed, strongly_connected_components_4)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {
        {0, 0}, {1, 1}, {2, 2}, {3, 3}};
    std::vector<local_stacks::Block> const pre_blocks = {
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 2}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 1},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 3},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 2}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 4},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Stop,
            .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{4}, {3, 2, 1, 0}});
}

TEST(poly_typed, strongly_connected_components_5)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {
        {0, 0}, {1, 1}, {2, 2}, {4, 4}};
    std::vector<local_stacks::Block> const pre_blocks = {
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 4}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 1},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 2},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 1}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 3},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Stop,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{3}, {4, 2, 1, 0}});
}

TEST(poly_typed, strongly_connected_components_6)
{
    std::unordered_map<byte_offset, block_id> const jumpdests = {
        {0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    std::vector<local_stacks::Block> const pre_blocks = {
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 3}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpI,
            .fallthrough_dest = 1},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpDest,
            .fallthrough_dest = 2},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 1}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 0}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::JumpDest,
            .fallthrough_dest = 4},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 5}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0},
        local_stacks::Block{
            .min_params = 0,
            .output = {Value{ValueIs::LITERAL, 3}},
            .instrs = {},
            .terminator = basic_blocks::Terminator::Jump,
            .fallthrough_dest = 0}};
    std::vector<Component> const components =
        strongly_connected_components(make_infer_state(jumpdests, pre_blocks));
    assert_components(components, {{2, 1}, {5, 4, 3}, {0}});
}
