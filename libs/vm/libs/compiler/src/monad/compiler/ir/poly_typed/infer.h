#pragma once

#include <monad/compiler/ir/poly_typed/unify.h>

namespace monad::compiler::poly_typed
{
    std::vector<Block> infer_types(
        std::unordered_map<byte_offset, block_id> const &jumpdests,
        std::vector<local_stacks::Block> const &pre_blocks);
}
