#pragma once

#include "unify.h"

namespace monad::compiler::poly_typed
{
    std::vector<Block>
    infer_component(InferState &state, Component const &component)
    {
        // TODO.
        // Remember to push fresh subst map on stack
        (void)component;
        (void)state;
        return {};
    }

    std::vector<Block> infer_components(
        InferState &state, std::vector<Component> const &components)
    {
        std::vector<Block> blocks;
        for (auto const &c : components) {
            auto new_blocks = infer_component(state, c);
            for (size_t i = 0; i < new_blocks.size(); ++i) {
                blocks.push_back(std::move(new_blocks[i]));
            }
        }
        return blocks;
    }

    std::vector<Block> infer_types(
        std::unordered_map<byte_offset, block_id> const &jumpdests,
        std::vector<local_stacks::Block> const &pre_blocks)
    {
        InferState state{
            .jumpdests = jumpdests,
            .pre_blocks = pre_blocks,
            .next_fresh_var_name = 0,
            .subst_maps = {},
            .block_types = {},
        };
        auto const components = strongly_connected_components(state);
        return infer_components(state, components);
    }
}
