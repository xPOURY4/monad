#pragma once

#include <monad/interpreter/call_runtime.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/call.hpp>
#include <monad/runtime/data.hpp>
#include <monad/runtime/math.hpp>
#include <monad/runtime/storage.hpp>
#include <monad/runtime/transmute.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/uint256.hpp>

#include <intx/intx.hpp>

#include <cstdint>

namespace monad::interpreter
{
    void stop(runtime::Context &, State &);
    void error(runtime::Context &, State &);

    template <std::size_t N>
        requires(N <= 32)
    void push(runtime::Context &, State &state)
    {
        state.push(runtime::uint256_load_bounded_le(state.instr_ptr + 1, N));
        state.instr_ptr += N + 1;
    }

    template <evmc_revision Rev>
    void sstore(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::sstore<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void sload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::sload<Rev>, ctx, state);
        state.next();
    }

    void calldataload(runtime::Context &ctx, State &state);

    template <evmc_revision Rev>
    void call(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::call<Rev>, ctx, state);
        state.next();
    }

    // Arithmetic

    void add(runtime::Context &, State &);
    void mul(runtime::Context &, State &);
    void sub(runtime::Context &, State &);
    void udiv(runtime::Context &, State &);
    void sdiv(runtime::Context &, State &);
    void umod(runtime::Context &, State &);
    void smod(runtime::Context &, State &);
    void addmod(runtime::Context &, State &);
    void mulmod(runtime::Context &, State &);

    template <evmc_revision Rev>
    void exp(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::exp<Rev>, ctx, state);
        state.next();
    }

    void signextend(runtime::Context &, State &);

    // Boolean

    void lt(runtime::Context &, State &);
    void gt(runtime::Context &, State &);
    void slt(runtime::Context &, State &);
    void sgt(runtime::Context &, State &);
    void eq(runtime::Context &, State &);
    void iszero(runtime::Context &, State &);

    // Bitwise
    void and_(runtime::Context &, State &);
    void or_(runtime::Context &, State &);
    void xor_(runtime::Context &, State &);
    void not_(runtime::Context &, State &);
    void byte(runtime::Context &, State &);
    void shl(runtime::Context &, State &);
    void shr(runtime::Context &, State &);
    void sar(runtime::Context &, State &);

    // Stack
    void pop(runtime::Context &, State &);

    template <std::size_t N>
        requires(N >= 1)
    void dup(runtime::Context &, State &state)
    {
        state.push(*(state.stack_top - N));
        state.next();
    }

    template <std::size_t N>
        requires(N >= 1)
    void swap(runtime::Context &, State &state)
    {
        std::swap(state.top(), *(state.stack_top - N));
        state.next();
    }

    // Control Flow
    void jump(runtime::Context &, State &);
    void jumpi(runtime::Context &, State &);
    void jumpdest(runtime::Context &, State &);

    // VM Control
    void return_(runtime::Context &, State &);
}
