#pragma once

#include "poly_typed/block.h"

namespace monad::compiler::poly_typed
{
    class PolyTypedIR
    {
    public:
        PolyTypedIR(local_stacks::LocalStacksIR const &&ir);
        uint64_t codesize;
        std::unordered_map<byte_offset, block_id> jumpdests;
        std::vector<Block> blocks;
    };
}
