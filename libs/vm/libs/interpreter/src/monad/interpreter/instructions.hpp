#pragma once

#include <monad/interpreter/call_runtime.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/runtime.hpp>
#include <monad/utils/uint256.hpp>

#include <intx/intx.hpp>

#include <cstdint>

namespace monad::interpreter
{
    void stop(runtime::Context &, State &);
    void invalid(runtime::Context &, State &);

    template <std::size_t N>
        requires(N <= 32)
    void push(runtime::Context &, State &state)
    {
        state.push(runtime::uint256_load_immediate<N>(state.instr_ptr + 1));
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

    // Data
    void sha3(runtime::Context &, State &);
    void address(runtime::Context &, State &);

    template <evmc_revision Rev>
    void balance(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::balance<Rev>, ctx, state);
        state.next();
    }

    void origin(runtime::Context &, State &);
    void caller(runtime::Context &, State &);
    void callvalue(runtime::Context &, State &);
    void calldataload(runtime::Context &ctx, State &state);
    void calldatasize(runtime::Context &ctx, State &state);
    void calldatacopy(runtime::Context &ctx, State &state);
    void codesize(runtime::Context &ctx, State &state);
    void codecopy(runtime::Context &ctx, State &state);
    void gasprice(runtime::Context &ctx, State &state);

    template <evmc_revision Rev>
    void extcodesize(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::extcodesize<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void extcodecopy(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::extcodecopy<Rev>, ctx, state);
        state.next();
    }

    void returndatasize(runtime::Context &ctx, State &state);
    void returndatacopy(runtime::Context &ctx, State &state);

    template <evmc_revision Rev>
    void extcodehash(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::extcodehash<Rev>, ctx, state);
        state.next();
    }

    void blockhash(runtime::Context &ctx, State &state);
    void coinbase(runtime::Context &ctx, State &state);
    void timestamp(runtime::Context &ctx, State &state);
    void number(runtime::Context &ctx, State &state);
    void prevrandao(runtime::Context &ctx, State &state);
    void gaslimit(runtime::Context &ctx, State &state);
    void chainid(runtime::Context &ctx, State &state);
    void selfbalance(runtime::Context &ctx, State &state);
    void basefee(runtime::Context &ctx, State &state);
    void blobhash(runtime::Context &ctx, State &state);
    void blobbasefee(runtime::Context &ctx, State &state);

    // Memory & Storage
    void mload(runtime::Context &ctx, State &state);
    void mstore(runtime::Context &ctx, State &state);
    void mstore8(runtime::Context &ctx, State &state);
    void mcopy(runtime::Context &ctx, State &state);

    void tload(runtime::Context &ctx, State &state);
    void tstore(runtime::Context &ctx, State &state);

    // Execution State
    void pc(runtime::Context &ctx, State &state);
    void msize(runtime::Context &ctx, State &state);
    void gas(runtime::Context &ctx, State &state);

    // Stack
    void pop(runtime::Context &, State &);

    template <std::size_t N>
        requires(N >= 1)
    void dup(runtime::Context &, State &state)
    {
        state.push(*(state.stack_top - (N - 1)));
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

    // Logging
    template <std::size_t N>
        requires(N <= 4)
    void log(runtime::Context &ctx, State &state)
    {
        static constexpr auto impls = std::tuple{
            &runtime::log0,
            &runtime::log1,
            &runtime::log2,
            &runtime::log3,
            &runtime::log4,
        };

        call_runtime(std::get<N>(impls), ctx, state);
        state.next();
    }

    // Call & Create
    template <evmc_revision Rev>
    void create(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::create<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void call(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::call<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void callcode(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::callcode<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void delegatecall(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::delegatecall<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void create2(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::create2<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void staticcall(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::staticcall<Rev>, ctx, state);
        state.next();
    }

    // VM Control
    void return_(runtime::Context &, State &);
    void revert(runtime::Context &, State &);

    template <evmc_revision Rev>
    void selfdestruct(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::selfdestruct<Rev>, ctx, state);
    }
}
