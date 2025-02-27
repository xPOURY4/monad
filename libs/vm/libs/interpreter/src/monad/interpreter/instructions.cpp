#include <monad/interpreter/instructions.hpp>
#include <monad/interpreter/state.hpp>
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
        auto const &a = state.pop();
        auto &b = state.top();

        b = a + b;

        state.instr_ptr++;
    }
}
