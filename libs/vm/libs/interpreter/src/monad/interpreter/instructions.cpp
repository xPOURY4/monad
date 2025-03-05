#include <monad/interpreter/call_runtime.hpp>
#include <monad/interpreter/instructions.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/data.hpp>
#include <monad/runtime/environment.hpp>
#include <monad/runtime/keccak.hpp>
#include <monad/runtime/math.hpp>
#include <monad/runtime/runtime.hpp>
#include <monad/runtime/storage.hpp>
#include <monad/runtime/transmute.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <intx/intx.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace monad::interpreter
{
    using enum runtime::StatusCode;

    void stop(runtime::Context &ctx, State &)
    {
        ctx.exit(Success);
    }

    void invalid(runtime::Context &ctx, State &)
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

    void sha3(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::sha3, ctx, state);
        state.next();
    }

    void address(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_address(ctx.env.recipient));
        state.next();
    }

    void origin(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_address(ctx.env.tx_context.tx_origin));
        state.next();
    }

    void caller(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_address(ctx.env.sender));
        state.next();
    }

    void callvalue(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_bytes32(ctx.env.value));
        state.next();
    }

    void calldataload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::calldataload, ctx, state);
        state.next();
    }

    void calldatasize(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.input_data_size);
        state.next();
    }

    void calldatacopy(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::calldatacopy, ctx, state);
        state.next();
    }

    void codesize(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.code_size);
        state.next();
    }

    void codecopy(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::codecopy, ctx, state);
        state.next();
    }

    void gasprice(runtime::Context &ctx, State &state)
    {
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));
        state.next();
    }

    void returndatasize(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.return_data_size);
        state.next();
    }

    void returndatacopy(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::returndatacopy, ctx, state);
        state.next();
    }

    void blockhash(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::blockhash, ctx, state);
        state.next();
    }

    void coinbase(runtime::Context &ctx, State &state)
    {
        state.push(
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));
        state.next();
    }

    void timestamp(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.tx_context.block_timestamp);
        state.next();
    }

    void number(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.tx_context.block_number);
        state.next();
    }

    void prevrandao(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_bytes32(
            ctx.env.tx_context.block_prev_randao));
        state.next();
    }

    void gaslimit(runtime::Context &ctx, State &state)
    {
        state.push(ctx.env.tx_context.block_gas_limit);
        state.next();
    }

    void chainid(runtime::Context &ctx, State &state)
    {
        state.push(runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));
        state.next();
    }

    void selfbalance(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::selfbalance, ctx, state);
        state.next();
    }

    void basefee(runtime::Context &ctx, State &state)
    {
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));
        state.next();
    }

    void blobhash(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::blobhash, ctx, state);
        state.next();
    }

    void blobbasefee(runtime::Context &ctx, State &state)
    {
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));
        state.next();
    }

    void mload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::mload, ctx, state);
        state.next();
    }

    void mstore(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::mstore, ctx, state);
        state.next();
    }

    void mstore8(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::mstore8, ctx, state);
        state.next();
    }

    void mcopy(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::mcopy, ctx, state);
        state.next();
    }

    void tload(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::tload, ctx, state);
        state.next();
    }

    void tstore(runtime::Context &ctx, State &state)
    {
        call_runtime(runtime::tstore, ctx, state);
        state.next();
    }

    void pc(runtime::Context &, State &state)
    {
        state.push(state.instr_ptr - state.analysis.code());
        state.next();
    }

    void msize(runtime::Context &ctx, State &state)
    {
        state.push(ctx.memory.size);
        state.next();
    }

    void gas(runtime::Context &ctx, State &state)
    {
        state.push(ctx.gas_remaining);
        state.next();
    }

    void pop(runtime::Context &, State &state)
    {
        --state.stack_top;
        state.next();
    }

    void jump_impl(
        runtime::Context &ctx, State &state, utils::uint256_t const &target)
    {
        if (MONAD_COMPILER_UNLIKELY(
                target > std::numeric_limits<std::size_t>::max())) {
            ctx.exit(Error);
        }

        auto const jd = static_cast<std::size_t>(target);
        if (MONAD_COMPILER_UNLIKELY(!state.analysis.is_jumpdest(jd))) {
            ctx.exit(Error);
        }

        state.instr_ptr = state.analysis.code() + jd;
    }

    void jump(runtime::Context &ctx, State &state)
    {
        auto const &target = state.pop();
        jump_impl(ctx, state, target);
    }

    void jumpi(runtime::Context &ctx, State &state)
    {
        auto const &target = state.pop();
        auto const &cond = state.pop();

        if (cond) {
            jump_impl(ctx, state, target);
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

    void revert(runtime::Context &ctx, State &state)
    {
        for (auto *result_loc : {&ctx.result.offset, &ctx.result.size}) {
            std::copy_n(
                intx::as_bytes(state.pop()),
                32,
                reinterpret_cast<std::uint8_t *>(result_loc));
        }

        ctx.exit(Revert);
    }
}
