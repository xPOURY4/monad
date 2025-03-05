#include <monad/interpreter/instructions.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/math.hpp>
#include <monad/runtime/types.hpp>

namespace monad::interpreter
{
    using enum runtime::StatusCode;

    void stop(runtime::Context &ctx, State &)
    {
        ctx.exit(Success);
    }

    void error(runtime::Context &ctx, State &)
    {
        ctx.exit(Error);
    }

    void add(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = a + b;
        state.next();
    }

    void mul(runtime::Context &ctx, State &state)
    {
        call_runtime(monad_runtime_mul, ctx, state);
        state.next();
    }

    void calldataload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::calldataload, ctx, state);
        state.next();
    }
}
