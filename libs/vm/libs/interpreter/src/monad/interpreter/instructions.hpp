#pragma once

#include <monad/interpreter/call_runtime.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/runtime.hpp>
#include <monad/utils/uint256.hpp>

#include <intx/intx.hpp>

#include <cstdint>
#include <memory>

namespace monad::interpreter
{
    using enum runtime::StatusCode;
    using enum compiler::EvmOpCode;

    template <std::uint8_t Instr, evmc_revision Rev>
    [[gnu::always_inline]] inline std::int64_t check_requirements(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        static constexpr auto info = compiler::opcode_table<Rev>[Instr];

        if constexpr (info.min_gas > 0) {
            gas_remaining -= info.min_gas;

            if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
                ctx.exit(Error);
            }
        }

        if constexpr (info.min_stack == 0 && !info.increases_stack) {
            return gas_remaining;
        }

        auto const stack_size = state.stack_top - stack_bottom;

        if constexpr (info.min_stack > 0) {
            if (MONAD_COMPILER_UNLIKELY(stack_size < info.min_stack)) {
                ctx.exit(Error);
            }
        }

        if constexpr (info.increases_stack) {
            static constexpr auto size_end = 1024 + info.min_stack;
            if (MONAD_COMPILER_UNLIKELY(stack_size >= size_end)) {
                ctx.exit(Error);
            }
        }

        return gas_remaining;
    }

    // Arithmetic
    template <evmc_revision Rev>
    std::int64_t
    add(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<ADD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = runtime::unrolled_add(a, b);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    mul(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MUL, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(monad_runtime_mul, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    sub(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SUB, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a - b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t udiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<DIV, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::udiv, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t sdiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<DIV, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::sdiv, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t umod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MOD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::umod, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t smod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SMOD, Rev>(
            ctx, state, stack_bottom, gas_remaining);

        gas_remaining = call_runtime(runtime::smod, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t addmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<ADDMOD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::addmod, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t mulmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MULMOD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::mulmod, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    exp(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<EXP, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::exp<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t signextend(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SIGNEXTEND, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[b, x] = state.pop_for_overwrite();
        x = monad::utils::signextend(b, x);
        state.next();
        return gas_remaining;
    }

    // Boolean
    template <evmc_revision Rev>
    std::int64_t
    lt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<LT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a < b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    gt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<GT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a > b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    slt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SLT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = intx::slt(a, b);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    sgt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SGT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = intx::slt(b, a); // note swapped arguments
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    eq(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<EQ, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = (a == b);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t iszero(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<ISZERO, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &a = state.top();
        a = (a == 0);
        state.next();
        return gas_remaining;
    }

    // Bitwise
    template <evmc_revision Rev>
    std::int64_t and_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<AND, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a & b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    or_(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<OR, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a | b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t xor_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<XOR, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a ^ b;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t not_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<NOT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &a = state.top();
        a = ~a;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t byte(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BYTE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[i, x] = state.pop_for_overwrite();
        x = utils::byte(i, x);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    shl(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SHL, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[shift, value] = state.pop_for_overwrite();
        value <<= shift;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    shr(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SHR, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[shift, value] = state.pop_for_overwrite();
        value >>= shift;
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    sar(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SAR, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto &&[shift, value] = state.pop_for_overwrite();
        value = monad::utils::sar(shift, value);
        state.next();
        return gas_remaining;
    }

    // Data
    template <evmc_revision Rev>
    std::int64_t sha3(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SHA3, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::sha3, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t address(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<ADDRESS, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_address(ctx.env.recipient));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t balance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BALANCE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::balance<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t origin(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<ORIGIN, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_address(ctx.env.tx_context.tx_origin));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t caller(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLER, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_address(ctx.env.sender));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t callvalue(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLVALUE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_bytes32(ctx.env.value));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t calldataload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLDATALOAD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::calldataload, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t calldatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLDATASIZE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.input_data_size);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t calldatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLDATACOPY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::calldatacopy, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t codesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CODESIZE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.code_size);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t codecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CODECOPY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::codecopy, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t gasprice(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<GAS, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t extcodesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<EXTCODESIZE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::extcodesize<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t extcodecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<EXTCODECOPY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::extcodecopy<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t returndatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<RETURNDATASIZE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.return_data_size);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t returndatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<RETURNDATACOPY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::returndatacopy, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t extcodehash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<EXTCODEHASH, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::extcodehash<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t blockhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BLOCKHASH, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::blockhash, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t coinbase(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<COINBASE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t timestamp(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<TIMESTAMP, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.tx_context.block_timestamp);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t number(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<NUMBER, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.tx_context.block_number);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t prevrandao(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<DIFFICULTY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_bytes32(
            ctx.env.tx_context.block_prev_randao));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t gaslimit(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<GASLIMIT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.env.tx_context.block_gas_limit);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t chainid(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CHAINID, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t selfbalance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SELFBALANCE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::selfbalance, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t basefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BASEFEE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t blobhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BLOBHASH, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::blobhash, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t blobbasefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<BLOBBASEFEE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));
        state.next();
        return gas_remaining;
    }

    // Memory & Storage
    template <evmc_revision Rev>
    std::int64_t mload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MLOAD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::mload, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t mstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MSTORE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::mstore, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t mstore8(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MSTORE8, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::mstore8, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t mcopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MCOPY, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::mcopy, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t tload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<TLOAD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining = call_runtime(runtime::tload, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t sstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SSTORE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::sstore<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t sload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SLOAD, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::sload<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t tstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<TSTORE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::tstore, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    // Execution State
    template <evmc_revision Rev>
    std::int64_t
    pc(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<PC, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(state.instr_ptr - state.analysis.code());
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t msize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<MSIZE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(ctx.memory.size);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    gas(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<GAS, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(gas_remaining);
        state.next();
        return gas_remaining;
    }

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    std::int64_t push(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        using subword_t = utils::uint256_t::word_type;

        // We need to do this memcpy dance to avoid triggering UB when
        // reading whole words from potentially unaligned addresses in the
        // instruction stream. The compilers seem able to optimise this out
        // effectively, and the generated code doesn't appear different to the
        // UB-triggering version.
        constexpr auto read_unaligned = [](std::uint8_t const *ptr) {
            alignas(subword_t) std::uint8_t aligned_mem[sizeof(subword_t)];
            std::memcpy(&aligned_mem[0], ptr, sizeof(subword_t));
            return std::byteswap(
                *reinterpret_cast<subword_t *>(&aligned_mem[0]));
        };

        static constexpr auto whole_words = N / 8;
        static constexpr auto leading_part = N % 8;

        gas_remaining = check_requirements<PUSH0 + N, Rev>(
            ctx, state, stack_bottom, gas_remaining);

        if constexpr (N == 0) {
            state.push(0);
        }
        else if constexpr (whole_words == 4) {
            static_assert(leading_part == 0);
            state.push(utils::uint256_t{
                read_unaligned(state.instr_ptr + 1 + 24),
                read_unaligned(state.instr_ptr + 1 + 16),
                read_unaligned(state.instr_ptr + 1 + 8),
                read_unaligned(state.instr_ptr + 1),
            });
        }
        else {
            auto const leading_word = [&state] {
                auto word = subword_t{0};

                if constexpr (leading_part == 0) {
                    return word;
                }

                std::memcpy(
                    reinterpret_cast<std::uint8_t *>(&word) +
                        (8 - leading_part),
                    state.instr_ptr + 1,
                    leading_part);

                return std::byteswap(word);
            }();

            if constexpr (whole_words == 0) {
                state.push(utils::uint256_t{leading_word, 0, 0, 0});
            }
            else if constexpr (whole_words == 1) {
                state.push(utils::uint256_t{
                    read_unaligned(state.instr_ptr + 1 + leading_part),
                    leading_word,
                    0,
                    0,
                });
            }
            else if constexpr (whole_words == 2) {
                state.push(utils::uint256_t{
                    read_unaligned(state.instr_ptr + 1 + 8 + leading_part),
                    read_unaligned(state.instr_ptr + 1 + leading_part),
                    leading_word,
                    0,
                });
            }
            else if constexpr (whole_words == 3) {
                state.push(utils::uint256_t{
                    read_unaligned(state.instr_ptr + 1 + 16 + leading_part),
                    read_unaligned(state.instr_ptr + 1 + 8 + leading_part),
                    read_unaligned(state.instr_ptr + 1 + leading_part),
                    leading_word,
                });
            }
        }

        state.instr_ptr += N + 1;
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t
    pop(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<POP, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        --state.stack_top;
        state.next();
        return gas_remaining;
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    std::int64_t
    dup(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<DUP1 + (N - 1), Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.push(*(state.stack_top - (N - 1)));
        state.next();
        return gas_remaining;
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    std::int64_t swap(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SWAP1 + (N - 1), Rev>(
            ctx, state, stack_bottom, gas_remaining);
        std::swap(state.top(), *(state.stack_top - N));
        state.next();
        return gas_remaining;
    }

    // Control Flow
    namespace
    {
        inline void jump_impl(
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
    }

    template <evmc_revision Rev>
    std::int64_t jump(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<JUMP, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto const &target = state.pop();
        jump_impl(ctx, state, target);
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t jumpi(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<JUMPI, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        auto const &target = state.pop();
        auto const &cond = state.pop();

        if (cond) {
            jump_impl(ctx, state, target);
            return gas_remaining;
        }
        else {
            state.next();
            return gas_remaining;
        }
    }

    template <evmc_revision Rev>
    std::int64_t jumpdest(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<JUMPDEST, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        state.next();
        return gas_remaining;
    }

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    std::int64_t
    log(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<LOG0 + N, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        static constexpr auto impls = std::tuple{
            &runtime::log0,
            &runtime::log1,
            &runtime::log2,
            &runtime::log3,
            &runtime::log4,
        };

        gas_remaining =
            call_runtime(std::get<N>(impls), ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    // Call & Create
    template <evmc_revision Rev>
    std::int64_t create(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CREATE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::create<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t call(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALL, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::call<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t callcode(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CALLCODE, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::callcode<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t delegatecall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<DELEGATECALL, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::delegatecall<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t create2(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<CREATE2, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::create2<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    template <evmc_revision Rev>
    std::int64_t staticcall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<STATICCALL, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::staticcall<Rev>, ctx, state, gas_remaining);
        state.next();
        return gas_remaining;
    }

    // VM Control
    namespace
    {
        inline void return_impl [[noreturn]] (
            runtime::StatusCode const code, runtime::Context &ctx, State &state,
            std::int64_t gas_remaining)
        {
            for (auto *result_loc : {&ctx.result.offset, &ctx.result.size}) {
                std::copy_n(
                    intx::as_bytes(state.pop()),
                    32,
                    reinterpret_cast<std::uint8_t *>(result_loc));
            }

            ctx.gas_remaining = gas_remaining;
            ctx.exit(code);
        }
    }

    template <evmc_revision Rev>
    std::int64_t return_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<RETURN, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        return_impl(Success, ctx, state, gas_remaining);
    }

    template <evmc_revision Rev>
    std::int64_t revert(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<REVERT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        return_impl(Revert, ctx, state, gas_remaining);
    }

    template <evmc_revision Rev>
    std::int64_t selfdestruct(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, std::int64_t gas_remaining)
    {
        gas_remaining = check_requirements<SELFDESTRUCT, Rev>(
            ctx, state, stack_bottom, gas_remaining);
        gas_remaining =
            call_runtime(runtime::selfdestruct<Rev>, ctx, state, gas_remaining);
        return gas_remaining;
    }

    inline std::int64_t stop(
        runtime::Context &ctx, State &, utils::uint256_t const *,
        std::int64_t gas_remaining)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Success);
    }

    inline std::int64_t invalid(
        runtime::Context &ctx, State &, utils::uint256_t const *,
        std::int64_t gas_remaining)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Error);
    }
}
