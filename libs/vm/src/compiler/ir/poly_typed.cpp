#include "compiler/ir/poly_typed.h"
#include "compiler/ir/local_stacks.h"
#include "poly_typed/infer.h"
#include <utility>

namespace monad::compiler::poly_typed
{
    PolyTypedIR::PolyTypedIR(local_stacks::LocalStacksIR const &&ir)
        : codesize{ir.codesize}
        , jumpdests{std::move(ir.jumpdests)}
        , blocks{infer_types(jumpdests, ir.blocks)}
    {
    }
}
