#pragma once

#include <monad/interpreter/state.hpp>
#include <monad/runtime/storage.hpp>
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
        state.push(utils::from_bytes(N, state.instr_ptr));

        state.instr_ptr += N;
    }

    template <evmc_revision Rev>
    void sstore(runtime::Context &ctx, State &state)
    {
        runtime::sstore<Rev>(&ctx, state.stack_top, state.stack_top - 1, 0);
        state.stack_top -= 2;
        state.stack_size -= 2;
    }

    void add(runtime::Context &, State &);
}
