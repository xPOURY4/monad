#pragma once

#include "subst_map.h"

#include <optional>
#include <unordered_map>

namespace monad::compiler::poly_typed
{
    struct InferState
    {
        std::unordered_map<byte_offset, block_id> const &jumpdests;
        std::vector<local_stacks::Block> const &pre_blocks;
        VarName next_fresh_var_name;
        std::vector<SubstMap> subst_maps;
        std::unordered_map<byte_offset, ContKind> block_types;
    };

    std::optional<block_id>
    static_jumpdests(InferState const &state, Value const &value);

    std::vector<block_id>
    static_successors(InferState const &state, block_id b);
}
