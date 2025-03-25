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
    [[gnu::always_inline]] inline void check_requirements(
        runtime::Context &ctx, State &, utils::uint256_t const *stack_bottom,
        utils::uint256_t *stack_top, std::int64_t &gas_remaining)
    {
        (void)stack_top;

        static constexpr auto info = compiler::opcode_table<Rev>[Instr];

        if constexpr (info.min_gas > 0) {
            gas_remaining -= info.min_gas;

            if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
                ctx.exit(Error);
            }
        }

        if constexpr (info.min_stack == 0 && !info.increases_stack) {
            return;
        }

        auto const stack_size = stack_top - stack_bottom;

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
    }

    [[gnu::always_inline]] inline void
    push(utils::uint256_t *&stack_top, utils::uint256_t const &x)
    {
        *++stack_top = x;
    }

    [[gnu::always_inline]] inline utils::uint256_t &
    pop(utils::uint256_t *&stack_top)
    {
        return *stack_top--;
    }

    [[gnu::always_inline]] inline auto
    pop_for_overwrite(utils::uint256_t *&stack_top)
    {
        auto const &a = pop(stack_top);
        return std::tie(a, *stack_top);
    }

    template <std::uint8_t Opcode, evmc_revision Rev, typename... FnArgs>
    [[gnu::always_inline]] inline OpcodeResult checked_runtime_call(
        void (*f)(FnArgs...), runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<Opcode, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        call_runtime(f, ctx, stack_top, gas_remaining);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Arithmetic
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    add(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<ADD, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = runtime::unrolled_add(a, b);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    mul(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MUL, Rev>(
            monad_runtime_mul,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sub(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SUB, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a - b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult udiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<DIV, Rev>(
            runtime::udiv, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sdiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SDIV, Rev>(
            runtime::sdiv, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult umod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MOD, Rev>(
            runtime::umod, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult smod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SMOD, Rev>(
            runtime::smod, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult addmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<ADDMOD, Rev>(
            runtime::addmod,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mulmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MULMOD, Rev>(
            runtime::mulmod,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    exp(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<EXP, Rev>(
            runtime::exp<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult signextend(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SIGNEXTEND, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[b, x] = pop_for_overwrite(stack_top);
        x = monad::utils::signextend(b, x);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Boolean
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    lt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining)
    {
        check_requirements<LT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a < b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    gt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining)
    {
        check_requirements<GT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a > b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    slt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SLT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = intx::slt(a, b);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sgt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SGT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = intx::slt(b, a); // note swapped arguments
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    eq(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining)
    {
        check_requirements<EQ, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = (a == b);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult iszero(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<ISZERO, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = (a == 0);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Bitwise
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult and_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<AND, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a & b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    or_(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<OR, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a | b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult xor_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<XOR, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a ^ b;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult not_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<NOT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = ~a;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult byte(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<BYTE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[i, x] = pop_for_overwrite(stack_top);
        x = utils::byte(i, x);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    shl(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SHL, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value <<= shift;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    shr(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SHR, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value >>= shift;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sar(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SAR, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value = monad::utils::sar(shift, value);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Data
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sha3(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SHA3, Rev>(
            runtime::sha3, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult address(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<ADDRESS, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.recipient));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult balance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<BALANCE, Rev>(
            runtime::balance<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult origin(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<ORIGIN, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.tx_origin));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult caller(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<CALLER, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.sender));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult callvalue(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<CALLVALUE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_bytes32(ctx.env.value));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult calldataload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CALLDATALOAD, Rev>(
            runtime::calldataload,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult calldatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<CALLDATASIZE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.input_data_size);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult calldatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CALLDATACOPY, Rev>(
            runtime::calldatacopy,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult codesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<CODESIZE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.code_size);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult codecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CODECOPY, Rev>(
            runtime::codecopy,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult gasprice(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<GAS, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<EXTCODESIZE, Rev>(
            runtime::extcodesize<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<EXTCODECOPY, Rev>(
            runtime::extcodecopy<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult returndatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<RETURNDATASIZE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.return_data_size);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult returndatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<RETURNDATACOPY, Rev>(
            runtime::returndatacopy,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodehash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<EXTCODEHASH, Rev>(
            runtime::extcodehash<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult blockhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<BLOCKHASH, Rev>(
            runtime::blockhash,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult coinbase(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<COINBASE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult timestamp(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<TIMESTAMP, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_timestamp);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult number(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<NUMBER, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_number);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult prevrandao(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<DIFFICULTY, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(
                ctx.env.tx_context.block_prev_randao));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult gaslimit(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<GASLIMIT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_gas_limit);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult chainid(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<CHAINID, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult selfbalance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SELFBALANCE, Rev>(
            runtime::selfbalance,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult basefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<BASEFEE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult blobhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<BLOBHASH, Rev>(
            runtime::blobhash,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult blobbasefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<BLOBBASEFEE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));
        state.next();
        return {gas_remaining, stack_top};
    }

    // Memory & Storage
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MLOAD, Rev>(
            runtime::mload, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MSTORE, Rev>(
            runtime::mstore,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mstore8(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MSTORE8, Rev>(
            runtime::mstore8,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mcopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<MCOPY, Rev>(
            runtime::mcopy, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SSTORE, Rev>(
            runtime::sstore<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SLOAD, Rev>(
            runtime::sload<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult tstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<TSTORE, Rev>(
            runtime::tstore,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult tload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<TLOAD, Rev>(
            runtime::tload, ctx, state, stack_bottom, stack_top, gas_remaining);
    }

    // Execution State
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    pc(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining)
    {
        check_requirements<PC, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, state.instr_ptr - state.analysis.code());
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult msize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<MSIZE, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.memory.size);
        state.next();
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    gas(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<GAS, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        push(stack_top, gas_remaining);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    [[gnu::noinline]] OpcodeResult push(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
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

        check_requirements<PUSH0 + N, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);

        if constexpr (N == 0) {
            push(stack_top, 0);
        }
        else if constexpr (whole_words == 4) {
            static_assert(leading_part == 0);
            push(
                stack_top,
                utils::uint256_t{
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
                push(stack_top, utils::uint256_t{leading_word, 0, 0, 0});
            }
            else if constexpr (whole_words == 1) {
                push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(state.instr_ptr + 1 + leading_part),
                        leading_word,
                        0,
                        0,
                    });
            }
            else if constexpr (whole_words == 2) {
                push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(state.instr_ptr + 1 + 8 + leading_part),
                        read_unaligned(state.instr_ptr + 1 + leading_part),
                        leading_word,
                        0,
                    });
            }
            else if constexpr (whole_words == 3) {
                push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(state.instr_ptr + 1 + 16 + leading_part),
                        read_unaligned(state.instr_ptr + 1 + 8 + leading_part),
                        read_unaligned(state.instr_ptr + 1 + leading_part),
                        leading_word,
                    });
            }
        }

        state.instr_ptr += N + 1;
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    pop(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<POP, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        --stack_top;
        state.next();
        return {gas_remaining, stack_top};
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    [[gnu::noinline]] OpcodeResult
    dup(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<DUP1 + (N - 1), Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);

        auto const old_top = stack_top;
        push(stack_top, *(old_top - (N - 1)));

        state.next();
        return {gas_remaining, stack_top};
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    [[gnu::noinline]] OpcodeResult swap(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<SWAP1 + (N - 1), Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        std::swap(*stack_top, *(stack_top - N));
        state.next();
        return {gas_remaining, stack_top};
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
    [[gnu::noinline]] OpcodeResult jump(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<JUMP, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto const &target = pop(stack_top);
        jump_impl(ctx, state, target);
        return {gas_remaining, stack_top};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult jumpi(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<JUMPI, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        auto const &target = pop(stack_top);
        auto const &cond = pop(stack_top);

        if (cond) {
            jump_impl(ctx, state, target);
            return {gas_remaining, stack_top};
        }
        else {
            state.next();
            return {gas_remaining, stack_top};
        }
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult jumpdest(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<JUMPDEST, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        state.next();
        return {gas_remaining, stack_top};
    }

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    [[gnu::noinline]] OpcodeResult
    log(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        static constexpr auto impls = std::tuple{
            &runtime::log0,
            &runtime::log1,
            &runtime::log2,
            &runtime::log3,
            &runtime::log4,
        };

        return checked_runtime_call<LOG0 + N, Rev>(
            std::get<N>(impls),
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    // Call & Create
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult create(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CREATE, Rev>(
            runtime::create<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult call(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CALL, Rev>(
            runtime::call<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult callcode(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CALLCODE, Rev>(
            runtime::callcode<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult delegatecall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<DELEGATECALL, Rev>(
            runtime::delegatecall<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult create2(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<CREATE2, Rev>(
            runtime::create2<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult staticcall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<STATICCALL, Rev>(
            runtime::staticcall<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    // VM Control
    namespace
    {
        inline void return_impl [[noreturn]] (
            runtime::StatusCode const code, runtime::Context &ctx,
            utils::uint256_t *stack_top, std::int64_t gas_remaining)
        {
            for (auto *result_loc : {&ctx.result.offset, &ctx.result.size}) {
                std::copy_n(
                    intx::as_bytes(*stack_top--),
                    32,
                    reinterpret_cast<std::uint8_t *>(result_loc));
            }

            ctx.gas_remaining = gas_remaining;
            ctx.exit(code);
        }
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult return_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<RETURN, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        return_impl(Success, ctx, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult revert(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        check_requirements<REVERT, Rev>(
            ctx, state, stack_bottom, stack_top, gas_remaining);
        return_impl(Revert, ctx, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult selfdestruct(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining)
    {
        return checked_runtime_call<SELFDESTRUCT, Rev>(
            runtime::selfdestruct<Rev>,
            ctx,
            state,
            stack_bottom,
            stack_top,
            gas_remaining);
    }

    [[gnu::noinline]] inline OpcodeResult stop(
        runtime::Context &ctx, State &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t gas_remaining)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Success);
    }

    [[gnu::noinline]] inline OpcodeResult invalid(
        runtime::Context &ctx, State &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t gas_remaining)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Error);
    }
}
