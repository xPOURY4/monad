#pragma once

#include <monad/vm/interpreter/call_runtime.hpp>
#include <monad/vm/interpreter/stack.hpp>
#include <monad/vm/runtime/runtime.hpp>
#include <monad/vm/utils/debug.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <cstdint>
#include <memory>

namespace monad::vm::interpreter
{
    using enum runtime::StatusCode;
    using enum compiler::EvmOpCode;

    template <std::uint8_t Opcode, evmc_revision Rev, typename... FnArgs>
    [[gnu::always_inline]] inline OpcodeResult checked_runtime_call(
        void (*f)(FnArgs...), runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<Opcode, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        call_runtime(f, ctx, stack_top, gas_remaining);
        return {gas_remaining, instr_ptr + 1};
    }

#ifdef MONAD_COMPILER_TESTING
    inline void fuzz_tstore_stack(
        runtime::Context const &ctx, utils::uint256_t const *stack_bottom,
        utils::uint256_t const *stack_top, std::uint64_t base_offset)
    {
        if (!utils::is_fuzzing_monad_vm) {
            return;
        }
        monad::vm::runtime::debug_tstore_stack(
            &ctx,
            stack_top + 1,
            static_cast<uint64_t>(stack_top - stack_bottom),
            0,
            base_offset);
    }
#else
    [[gnu::always_inline]] inline void fuzz_tstore_stack(
        runtime::Context const &, utils::uint256_t const *,
        utils::uint256_t const *, std::uint64_t)
    {
        // nop
    }
#endif

    // Arithmetic
    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult
    add(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ADD, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a + b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    mul(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<MUL, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b *= a;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sub(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SUB, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a - b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult udiv(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<DIV, Rev>(
            runtime::udiv,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sdiv(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SDIV, Rev>(
            runtime::sdiv,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult umod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MOD, Rev>(
            runtime::umod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult smod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SMOD, Rev>(
            runtime::smod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult addmod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<ADDMOD, Rev>(
            runtime::addmod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mulmod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MULMOD, Rev>(
            runtime::mulmod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    exp(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<EXP, Rev>(
            runtime::exp<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult signextend(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SIGNEXTEND, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[b, x] = pop_for_overwrite(stack_top);
        x = utils::signextend(b, x);
        return {gas_remaining, instr_ptr + 1};
    }

    // Boolean
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    lt(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<LT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a < b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    gt(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a > b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    slt(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SLT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = slt(a, b);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sgt(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SGT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = slt(b, a); // note swapped arguments
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    eq(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<EQ, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = (a == b);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult iszero(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ISZERO, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = !a;
        return {gas_remaining, instr_ptr + 1};
    }

    // Bitwise
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult and_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<AND, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a & b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    or_(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<OR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a | b;

        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult xor_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<XOR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = pop_for_overwrite(stack_top);
        b = a ^ b;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult not_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<NOT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = ~a;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult byte(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BYTE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[i, x] = pop_for_overwrite(stack_top);
        x = utils::byte(i, x);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    shl(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SHL, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value <<= shift;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    shr(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SHR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value >>= shift;
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    sar(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SAR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = pop_for_overwrite(stack_top);
        value = utils::sar(shift, value);
        return {gas_remaining, instr_ptr + 1};
    }

    // Data
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sha3(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SHA3, Rev>(
            runtime::sha3,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult address(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ADDRESS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.recipient));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult balance(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<BALANCE, Rev>(
            runtime::balance<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult origin(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ORIGIN, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.tx_origin));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult caller(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLER, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.sender));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult callvalue(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLVALUE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_bytes32(ctx.env.value));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult calldataload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CALLDATALOAD, Rev>(
            runtime::calldataload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult calldatasize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLDATASIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.input_data_size);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult calldatacopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CALLDATACOPY, Rev>(
            runtime::calldatacopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult codesize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CODESIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.code_size);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult codecopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CODECOPY, Rev>(
            runtime::codecopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult gasprice(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GAS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodesize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<EXTCODESIZE, Rev>(
            runtime::extcodesize<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodecopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<EXTCODECOPY, Rev>(
            runtime::extcodecopy<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult returndatasize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<RETURNDATASIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.return_data_size);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult returndatacopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<RETURNDATACOPY, Rev>(
            runtime::returndatacopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult extcodehash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<EXTCODEHASH, Rev>(
            runtime::extcodehash<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult blockhash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<BLOCKHASH, Rev>(
            runtime::blockhash,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult coinbase(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<COINBASE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult timestamp(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<TIMESTAMP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_timestamp);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult number(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<NUMBER, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_number);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult prevrandao(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<DIFFICULTY, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(
                ctx.env.tx_context.block_prev_randao));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult gaslimit(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GASLIMIT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_gas_limit);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult chainid(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CHAINID, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult selfbalance(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SELFBALANCE, Rev>(
            runtime::selfbalance,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult basefee(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BASEFEE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult blobhash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<BLOBHASH, Rev>(
            runtime::blobhash,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult blobbasefee(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BLOBBASEFEE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));
        return {gas_remaining, instr_ptr + 1};
    }

    // Memory & Storage
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MLOAD, Rev>(
            runtime::mload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MSTORE, Rev>(
            runtime::mstore,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mstore8(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MSTORE8, Rev>(
            runtime::mstore8,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult mcopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<MCOPY, Rev>(
            runtime::mcopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SSTORE, Rev>(
            runtime::sstore<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult sload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<SLOAD, Rev>(
            runtime::sload<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult tstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<TSTORE, Rev>(
            runtime::tstore,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult tload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<TLOAD, Rev>(
            runtime::tload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    // Execution Intercode
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult
    pc(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<PC, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, instr_ptr - analysis.code());
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult msize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<MSIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.memory.size);
        return {gas_remaining, instr_ptr + 1};
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult
    gas(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GAS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, gas_remaining);
        return {gas_remaining, instr_ptr + 1};
    }

    // Stack
    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult
    pop(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<POP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        --stack_top;
        return {gas_remaining, instr_ptr + 1};
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    [[gnu::always_inline]] inline OpcodeResult
    dup(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<DUP1 + (N - 1), Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);

        auto const old_top = stack_top;
        push(stack_top, *(old_top - (N - 1)));

        return {gas_remaining, instr_ptr + 1};
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    [[gnu::always_inline]] inline OpcodeResult swap(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SWAP1 + (N - 1), Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        std::swap(*stack_top, *(stack_top - N));
        return {gas_remaining, instr_ptr + 1};
    }

    // Control Flow
    namespace
    {
        inline std::uint8_t const *jump_impl(
            runtime::Context &ctx, Intercode const &analysis,
            utils::uint256_t const &target)
        {
            if (MONAD_VM_UNLIKELY(
                    target > std::numeric_limits<std::size_t>::max())) {
                ctx.exit(Error);
            }

            auto const jd = static_cast<std::size_t>(target);
            if (MONAD_VM_UNLIKELY(!analysis.is_jumpdest(jd))) {
                ctx.exit(Error);
            }

            return analysis.code() + jd;
        }
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult jump(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        check_requirements<JUMP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto const &target = pop(stack_top);
        auto const new_ip = jump_impl(ctx, analysis, target);
        return {gas_remaining, new_ip};
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult jumpi(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<JUMPI, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto const &target = pop(stack_top);
        auto const &cond = pop(stack_top);

        if (cond) {
            auto const new_ip = jump_impl(ctx, analysis, target);
            return {gas_remaining, new_ip};
        }
        else {
            return {gas_remaining, instr_ptr + 1};
        }
    }

    template <evmc_revision Rev>
    [[gnu::always_inline]] inline OpcodeResult jumpdest(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        fuzz_tstore_stack(
            ctx,
            stack_bottom,
            stack_top,
            static_cast<uint64_t>(instr_ptr - analysis.code()));
        check_requirements<JUMPDEST, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        return {gas_remaining, instr_ptr + 1};
    }

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    [[gnu::noinline]] OpcodeResult
    log(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
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
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    // Call & Create
    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult create(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CREATE, Rev>(
            runtime::create<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult call(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CALL, Rev>(
            runtime::call<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult callcode(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CALLCODE, Rev>(
            runtime::callcode<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult delegatecall(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<DELEGATECALL, Rev>(
            runtime::delegatecall<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult create2(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<CREATE2, Rev>(
            runtime::create2<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult staticcall(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return checked_runtime_call<STATICCALL, Rev>(
            runtime::staticcall<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
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
                    (*stack_top--).as_bytes(),
                    32,
                    reinterpret_cast<std::uint8_t *>(result_loc));
            }

            ctx.gas_remaining = gas_remaining;
            ctx.exit(code);
        }
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult return_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        fuzz_tstore_stack(ctx, stack_bottom, stack_top, analysis.code_size());
        check_requirements<RETURN, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        return_impl(Success, ctx, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult revert(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        check_requirements<REVERT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        return_impl(Revert, ctx, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    [[gnu::noinline]] OpcodeResult selfdestruct(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        fuzz_tstore_stack(ctx, stack_bottom, stack_top, analysis.code_size());
        return checked_runtime_call<SELFDESTRUCT, Rev>(
            runtime::selfdestruct<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    [[gnu::noinline]] inline OpcodeResult stop(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        fuzz_tstore_stack(ctx, stack_bottom, stack_top, analysis.code_size());
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Success);
    }

    [[gnu::noinline]] inline OpcodeResult invalid(
        runtime::Context &ctx, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t gas_remaining, std::uint8_t const *)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Error);
    }
}
