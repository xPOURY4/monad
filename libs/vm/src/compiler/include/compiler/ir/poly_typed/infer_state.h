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
        SubstMap subst_map;
        std::unordered_map<block_id, ContKind> block_types;
        std::unordered_map<block_id, Terminator> block_terminators;

        std::vector<block_id> static_successors(block_id b) const;

        VarName fresh()
        {
            return next_fresh_var_name++;
        }

        ContKind get_type(block_id);
    };
}
