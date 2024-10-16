#pragma once

#include <compiler/ir/local_stacks.h>

namespace monad::compiler::poly_typed
{
    using ValueIs = local_stacks::ValueIs;
    using Value = local_stacks::Value;
    using Instruction = bytecode::Instruction;
}
