#pragma once

#include "unify.h"

namespace monad::compiler::poly_typed
{
    std::vector<Block>
    infer_component(InferState &state, Component const &component);

    std::vector<Block> infer_components(
        InferState &state, std::vector<Component> const &components);

    std::vector<Block> infer_types(
        std::unordered_map<byte_offset, block_id> const &jumpdests,
        std::vector<local_stacks::Block> const &pre_blocks);
}
