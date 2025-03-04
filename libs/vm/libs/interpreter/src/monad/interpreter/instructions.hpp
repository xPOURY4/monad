#pragma once

#include <monad/interpreter/call_runtime.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/call.hpp>
#include <monad/runtime/data.hpp>
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
        state.instr_ptr++;
    }

    void calldataload(runtime::Context &ctx, State &state);

    template <evmc_revision Rev>
    void call(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::call<Rev>, ctx, state);
        state.instr_ptr++;
    }

    void add(runtime::Context &, State &);

    void mul(runtime::Context &, State &);
}
