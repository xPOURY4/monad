#include <monad/interpreter/instructions.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/math.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>

#include <intx/intx.hpp>

#include <format>
#include <iostream>

namespace monad::interpreter
#include <monad/utils/uint256.hpp>
{
    using enum runtime::StatusCode;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
        ctx.exit(Success);
    }

    void error(runtime::Context &, State &state)
    {
        std::cerr << std::format("Unknown op: {:02X}\n", *state.instr_ptr);
        abort();
        // ctx.exit(Error);
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

    void lt(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = (a < b) ? 1 : 0;
        state.next();
    }

    void gt(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = (a > b) ? 1 : 0;
        state.next();
    }

    void slt(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = intx::slt(a, b) ? 1 : 0;
        state.next();
    }

    void sgt(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = intx::slt(b, a) ? 1 : 0; // note swapped arguments
        state.next();
    }

    void eq(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = (a == b) ? 1 : 0;
        state.next();
    }

    void iszero(runtime::Context &, State &state)
    {
        auto &a = state.top();
        a = (a == 0) ? 1 : 0;
        state.next();
    }

    void and_(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = a & b;
        state.next();
    }

    void or_(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = a | b;
        state.next();
    }

    void xor_(runtime::Context &, State &state)
    {
        auto &&[a, b] = state.pop_for_overwrite<2>();
        b = a ^ b;
        state.next();
    }

    void not_(runtime::Context &, State &state)
    {
        auto &a = state.top();
        a = ~a;
        state.next();
    }

    void byte(runtime::Context &, State &state)
    {
        auto &&[i, x] = state.pop_for_overwrite<2>();
        x = utils::byte(i, x);
        state.next();
    }

    void shl(runtime::Context &, State &state)
    {
        auto &&[shift, value] = state.pop_for_overwrite<2>();
        value <<= shift;
        state.next();
    }

    void shr(runtime::Context &, State &state)
    {
        auto &&[shift, value] = state.pop_for_overwrite<2>();
        value >>= shift;
        state.next();
    }

    void sar(runtime::Context &, State &state)
    {
        auto &&[shift, value] = state.pop_for_overwrite<2>();
        value = monad::utils::sar(shift, value);
        state.next();
    }

    void calldataload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::calldataload, ctx, state);
        state.next();
    }

    void pop(runtime::Context &, State &state)
    {
        --state.stack_top;
        state.next();
    }

    void jump(runtime::Context &ctx, State &state)
    {
        auto const &jd_word = state.pop();

        MONAD_COMPILER_DEBUG_ASSERT(
            jd_word <= std::numeric_limits<std::size_t>::max());
        auto const jd = static_cast<std::size_t>(jd_word);

        if (MONAD_COMPILER_UNLIKELY(!state.analysis.is_jumpdest(jd))) {
            ctx.exit(Error);
        }

        state.instr_ptr = state.analysis.code() + jd;
    }

    void jumpi(runtime::Context &ctx, State &state)
    {
        auto const &jd_word = state.pop();
        auto const &cond = state.pop();

        if (cond) {
            MONAD_COMPILER_DEBUG_ASSERT(
                jd_word <= std::numeric_limits<std::size_t>::max());
            auto const jd = static_cast<std::size_t>(jd_word);

            if (MONAD_COMPILER_UNLIKELY(!state.analysis.is_jumpdest(jd))) {
                ctx.exit(Error);
            }

            state.instr_ptr = state.analysis.code() + jd;
        }
        else {
            state.next();
        }
    }

    void jumpdest(runtime::Context &, State &state)
    {
        state.next();
    }

    void return_(runtime::Context &ctx, State &state)
    {
        for (auto *result_loc : {&ctx.result.offset, &ctx.result.size}) {
            std::copy_n(
                intx::as_bytes(state.pop()),
                32,
                reinterpret_cast<std::uint8_t *>(result_loc));
        }

        ctx.exit(Success);
    }
}
