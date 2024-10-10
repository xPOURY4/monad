#pragma once

#include <compiler/ir/local_stacks.h>

namespace monad::compiler::poly_typed
{
    using ValueIs = local_stacks::ValueIs;
    using Value = local_stacks::Value;
    using Instruction = bytecode::Instruction;

    struct Block
    {
        // ContKind
        // TerminatorKind (variant)
        std::vector<Value> output;
        std::vector<Instruction> instrs;
    };

    class PolyTypedIR
    {
    public:
        PolyTypedIR(local_stacks::LocalStacksIR const &&ir);
        uint64_t codesize;
        std::unordered_map<byte_offset, block_id> jumpdests;
        std::vector<Block> blocks;
    };
}
