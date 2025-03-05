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

    void sub(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = a - b;
        state.next();
    }

    void udiv(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::udiv, ctx, state);
        state.next();
    }

    void sdiv(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::sdiv, ctx, state);
        state.next();
    }

    void umod(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::umod, ctx, state);
        state.next();
    }

    void smod(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::smod, ctx, state);
        state.next();
    }

    void addmod(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::addmod, ctx, state);
        state.next();
    }

    void mulmod(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::mulmod, ctx, state);
        state.next();
    }

    void signextend(runtime::Context &, State &state)
    {
        auto &&[b, x] = state.pop_for_overwrite<2>();
        x = monad::utils::signextend(b, x);
        state.next();
    }

    void calldataload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::calldataload, ctx, state);
        state.next();
    }
}
