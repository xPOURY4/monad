#pragma once

#include "kind.h"

#include <compiler/ir/local_stacks.h>

namespace monad::compiler::poly_typed
{
    using ValueIs = local_stacks::ValueIs;
    using Value = local_stacks::Value;
    using Instruction = bytecode::Instruction;

    struct FallThrough
    {
        ContKind fall_through_kind;
    };

    struct JumpI
    {
        ContKind fall_through_kind;
        ContKind jump_kind;
    };

    struct Jump
    {
        ContKind jump_kind;
    };

    struct Return
    {
    };

    struct Stop
    {
    };

    struct Revert
    {
    };

    struct SelfDestruct
    {
    };

    struct InvalidInstruction
    {
    };

    using Terminator = std::variant<
        FallThrough, JumpI, Jump, Return, Stop, Revert, SelfDestruct,
        InvalidInstruction>;

    struct Block
    {
        std::vector<Value> output;
        std::vector<Instruction> instrs;
        ContKind kind;
        Terminator terminator;
    };
}
