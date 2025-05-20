#pragma once

#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/interpreter/call_runtime.hpp>
#include <monad/vm/interpreter/instructions_fwd.hpp>
#include <monad/vm/interpreter/push.hpp>
#include <monad/vm/interpreter/stack.hpp>
#include <monad/vm/interpreter/types.hpp>
#include <monad/vm/runtime/runtime.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/runtime/uint256.hpp>
#include <monad/vm/utils/debug.hpp>

#include <intx/intx.hpp>

#include <evmc/evmc.h>

#include <array>
#include <cstdint>
#include <memory>

#if __has_attribute(musttail)
    #define MUSTTAIL __attribute__((musttail))
#else
    #error "No compiler support for __attribute__((musttail))"
#endif

#define NEXT(OP)                                                               \
    do {                                                                       \
        static constexpr auto delta =                                          \
            compiler::opcode_table<Rev>[(OP)].stack_increase -                 \
            compiler::opcode_table<Rev>[(OP)].min_stack;                       \
                                                                               \
        ++instr_ptr;                                                           \
        MUSTTAIL return instruction_table<Rev>[*instr_ptr](                    \
            ctx,                                                               \
            analysis,                                                          \
            stack_bottom,                                                      \
            stack_top + delta,                                                 \
            gas_remaining,                                                     \
            instr_ptr);                                                        \
    }                                                                          \
    while (false);

#define NEXT_PUSH(OP)                                                          \
    do {                                                                       \
        static constexpr auto delta =                                          \
            compiler::opcode_table<Rev>[(OP)].stack_increase -                 \
            compiler::opcode_table<Rev>[(OP)].min_stack;                       \
                                                                               \
        instr_ptr += (((OP) - PUSH0) + 1);                                     \
        MUSTTAIL return instruction_table<Rev>[*instr_ptr](                    \
            ctx,                                                               \
            analysis,                                                          \
            stack_bottom,                                                      \
            stack_top + delta,                                                 \
            gas_remaining,                                                     \
            instr_ptr);                                                        \
    }                                                                          \
    while (false);

namespace monad::vm::interpreter
{
    using enum runtime::StatusCode;
    using enum compiler::EvmOpCode;

    template <evmc_revision Rev>
    consteval InstrTable make_instruction_table()
    {
        constexpr auto since = [](evmc_revision first, InstrEval impl) {
            return (Rev >= first) ? impl : invalid;
        };

        return {
            stop, // 0x00
            add<Rev>, // 0x01
            mul<Rev>, // 0x02
            sub<Rev>, // 0x03
            udiv<Rev>, // 0x04,
            sdiv<Rev>, // 0x05,
            umod<Rev>, // 0x06,
            smod<Rev>, // 0x07,
            addmod<Rev>, // 0x08,
            mulmod<Rev>, // 0x09,
            exp<Rev>, // 0x0A,
            signextend<Rev>, // 0x0B,
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            lt<Rev>, // 0x10,
            gt<Rev>, // 0x11,
            slt<Rev>, // 0x12,
            sgt<Rev>, // 0x13,
            eq<Rev>, // 0x14,
            iszero<Rev>, // 0x15,
            and_<Rev>, // 0x16,
            or_<Rev>, // 0x17,
            xor_<Rev>, // 0x18,
            not_<Rev>, // 0x19,
            byte<Rev>, // 0x1A,
            since(EVMC_CONSTANTINOPLE, shl<Rev>), // 0x1B,
            since(EVMC_CONSTANTINOPLE, shr<Rev>), // 0x1C,
            since(EVMC_CONSTANTINOPLE, sar<Rev>), // 0x1D,
            invalid, //
            invalid, //

            sha3<Rev>, // 0x20,
            invalid, //
            invalid, //
            invalid, //
            invalid,
            //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            address<Rev>, // 0x30,
            balance<Rev>, // 0x31,
            origin<Rev>, // 0x32,
            caller<Rev>, // 0x33,
            callvalue<Rev>, // 0x34,
            calldataload<Rev>, // 0x35,
            calldatasize<Rev>, // 0x36,
            calldatacopy<Rev>, // 0x37,
            codesize<Rev>, // 0x38,
            codecopy<Rev>, // 0x39,
            gasprice<Rev>, // 0x3A,
            extcodesize<Rev>, // 0x3B,
            extcodecopy<Rev>, // 0x3C,
            since(EVMC_BYZANTIUM, returndatasize<Rev>), // 0x3D,
            since(EVMC_BYZANTIUM, returndatacopy<Rev>), // 0x3E,
            since(EVMC_CONSTANTINOPLE, extcodehash<Rev>), // 0x3F,

            blockhash<Rev>, // 0x40,
            coinbase<Rev>, // 0x41,
            timestamp<Rev>, // 0x42,
            number<Rev>, // 0x43,
            prevrandao<Rev>, // 0x44,
            gaslimit<Rev>, // 0x45,
            since(EVMC_ISTANBUL, chainid<Rev>), // 0x46,
            since(EVMC_ISTANBUL, selfbalance<Rev>), // 0x47,
            since(EVMC_LONDON, basefee<Rev>), // 0x48,
            since(EVMC_CANCUN, blobhash<Rev>), // 0x49,
            since(EVMC_CANCUN, blobbasefee<Rev>), // 0x4A,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            pop<Rev>, // 0x50,
            mload<Rev>, // 0x51,
            mstore<Rev>, // 0x52,
            mstore8<Rev>, // 0x53,
            sload<Rev>, // 0x54,
            sstore<Rev>, // 0x55,
            jump<Rev>, // 0x56,
            jumpi<Rev>, // 0x57,
            pc<Rev>, // 0x58,
            msize<Rev>, // 0x59,
            gas<Rev>, // 0x5A,
            jumpdest<Rev>, // 0x5B,
            since(EVMC_CANCUN, tload<Rev>), // 0x5C,
            since(EVMC_CANCUN, tstore<Rev>), // 0x5D,
            since(EVMC_CANCUN, mcopy<Rev>), // 0x5E,
            since(EVMC_SHANGHAI, push<0, Rev>), // 0x5F,

            push<1, Rev>, // 0x60,
            push<2, Rev>, // 0x61,
            push<3, Rev>, // 0x62,
            push<4, Rev>, // 0x63,
            push<5, Rev>, // 0x64,
            push<6, Rev>, // 0x65,
            push<7, Rev>, // 0x66,
            push<8, Rev>, // 0x67,
            push<9, Rev>, // 0x68,
            push<10, Rev>, // 0x69,
            push<11, Rev>, // 0x6A,
            push<12, Rev>, // 0x6B,
            push<13, Rev>, // 0x6C,
            push<14, Rev>, // 0x6D,
            push<15, Rev>, // 0x6E,
            push<16, Rev>, // 0x6F,

            push<17, Rev>, // 0x70,
            push<18, Rev>, // 0x71,
            push<19, Rev>, // 0x72,
            push<20, Rev>, // 0x73,
            push<21, Rev>, // 0x74,
            push<22, Rev>, // 0x75,
            push<23, Rev>, // 0x76,
            push<24, Rev>, // 0x77,
            push<25, Rev>, // 0x78,
            push<26, Rev>, // 0x79,
            push<27, Rev>, // 0x7A,
            push<28, Rev>, // 0x7B,
            push<29, Rev>, // 0x7C,
            push<30, Rev>, // 0x7D,
            push<31, Rev>, // 0x7E,
            push<32, Rev>, // 0x7F,

            dup<1, Rev>, // 0x80,
            dup<2, Rev>, // 0x81,
            dup<3, Rev>, // 0x82,
            dup<4, Rev>, // 0x83,
            dup<5, Rev>, // 0x84,
            dup<6, Rev>, // 0x85,
            dup<7, Rev>, // 0x86,
            dup<8, Rev>, // 0x87,
            dup<9, Rev>, // 0x88,
            dup<10, Rev>, // 0x89,
            dup<11, Rev>, // 0x8A,
            dup<12, Rev>, // 0x8B,
            dup<13, Rev>, // 0x8C,
            dup<14, Rev>, // 0x8D,
            dup<15, Rev>, // 0x8E,
            dup<16, Rev>, // 0x8F,

            swap<1, Rev>, // 0x90,
            swap<2, Rev>, // 0x91,
            swap<3, Rev>, // 0x92,
            swap<4, Rev>, // 0x93,
            swap<5, Rev>, // 0x94,
            swap<6, Rev>, // 0x95,
            swap<7, Rev>, // 0x96,
            swap<8, Rev>, // 0x97,
            swap<9, Rev>, // 0x98,
            swap<10, Rev>, // 0x99,
            swap<11, Rev>, // 0x9A,
            swap<12, Rev>, // 0x9B,
            swap<13, Rev>, // 0x9C,
            swap<14, Rev>, // 0x9D,
            swap<15, Rev>, // 0x9E,
            swap<16, Rev>, // 0x9F,

            log<0, Rev>, // 0xA0,
            log<1, Rev>, // 0xA1,
            log<2, Rev>, // 0xA2,
            log<3, Rev>, // 0xA3,
            log<4, Rev>, // 0xA4,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            invalid, //

            create<Rev>, // 0xF0,
            call<Rev>, // 0xF1,
            callcode<Rev>, // 0xF2,
            return_<Rev>, // 0xF3,
            since(EVMC_HOMESTEAD, delegatecall<Rev>), // 0xF4,
            since(EVMC_CONSTANTINOPLE, create2<Rev>), // 0xF5,
            invalid, //
            invalid, //
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, staticcall<Rev>), // 0xFA,
            invalid, //
            invalid, //
            since(EVMC_BYZANTIUM, revert<Rev>), // 0xFD,
            invalid, // 0xFE,
            selfdestruct<Rev>, // 0xFF,
        };
    }

    template <evmc_revision Rev>
    constexpr InstrTable instruction_table = make_instruction_table<Rev>();

    // Instruction implementations
    template <std::uint8_t Opcode, evmc_revision Rev, typename... FnArgs>
    [[gnu::always_inline]] inline void checked_runtime_call(
        void (*f)(FnArgs...), runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t &gas_remaining, std::uint8_t const *)
    {
        check_requirements<Opcode, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        call_runtime(f, ctx, stack_top, gas_remaining);
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
    void fuzz_tstore_stack(
        runtime::Context const &, utils::uint256_t const *,
        utils::uint256_t const *, std::uint64_t)
    {
        // nop
    }
#endif

    // Arithmetic
    template <evmc_revision Rev>
    void
    add(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ADD, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = runtime::unrolled_add(a, b);

        NEXT(ADD);
    }

    template <evmc_revision Rev>
    void
    mul(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MUL, Rev>(
            monad_vm_runtime_mul,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MUL);
    }

    template <evmc_revision Rev>
    void
    sub(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SUB, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a - b;

        NEXT(SUB);
    }

    template <evmc_revision Rev>
    void udiv(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<DIV, Rev>(
            runtime::udiv,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(DIV);
    }

    template <evmc_revision Rev>
    void sdiv(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SDIV, Rev>(
            runtime::sdiv,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SDIV);
    }

    template <evmc_revision Rev>
    void umod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MOD, Rev>(
            runtime::umod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MOD);
    }

    template <evmc_revision Rev>
    void smod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SMOD, Rev>(
            runtime::smod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SMOD);
    }

    template <evmc_revision Rev>
    void addmod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<ADDMOD, Rev>(
            runtime::addmod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(ADDMOD);
    }

    template <evmc_revision Rev>
    void mulmod(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MULMOD, Rev>(
            runtime::mulmod,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MULMOD);
    }

    template <evmc_revision Rev>
    void
    exp(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<EXP, Rev>(
            runtime::exp<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(EXP);
    }

    template <evmc_revision Rev>
    void signextend(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SIGNEXTEND, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[b, x] = top_two(stack_top);
        x = utils::signextend(b, x);

        NEXT(SIGNEXTEND);
    }

    // Boolean
    template <evmc_revision Rev>
    void
    lt(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<LT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a < b;

        NEXT(LT);
    }

    template <evmc_revision Rev>
    void
    gt(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a > b;

        NEXT(GT);
    }

    template <evmc_revision Rev>
    void
    slt(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SLT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = intx::slt(a, b);

        NEXT(SLT);
    }

    template <evmc_revision Rev>
    void
    sgt(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SGT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = intx::slt(b, a); // note swapped arguments

        NEXT(SGT);
    }

    template <evmc_revision Rev>
    void
    eq(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<EQ, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = (a == b);

        NEXT(EQ);
    }

    template <evmc_revision Rev>
    void iszero(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ISZERO, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = !a;

        NEXT(ISZERO);
    }

    // Bitwise
    template <evmc_revision Rev>
    void and_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<AND, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a & b;

        NEXT(AND);
    }

    template <evmc_revision Rev>
    void
    or_(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<OR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a | b;

        NEXT(OR);
    }

    template <evmc_revision Rev>
    void xor_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<XOR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[a, b] = top_two(stack_top);
        b = a ^ b;

        NEXT(XOR);
    }

    template <evmc_revision Rev>
    void not_(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<NOT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &a = *stack_top;
        a = ~a;

        NEXT(NOT);
    }

    template <evmc_revision Rev>
    void byte(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BYTE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[i, x] = top_two(stack_top);
        x = utils::byte(i, x);

        NEXT(BYTE);
    }

    template <evmc_revision Rev>
    void
    shl(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SHL, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = top_two(stack_top);
        value <<= shift;

        NEXT(SHL);
    }

    template <evmc_revision Rev>
    void
    shr(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SHR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = top_two(stack_top);
        value >>= shift;

        NEXT(SHR);
    }

    template <evmc_revision Rev>
    void
    sar(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SAR, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto &&[shift, value] = top_two(stack_top);
        value = utils::sar(shift, value);

        NEXT(SAR);
    }

    // Data
    template <evmc_revision Rev>
    void sha3(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SHA3, Rev>(
            runtime::sha3,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SHA3);
    }

    template <evmc_revision Rev>
    void address(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ADDRESS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.recipient));

        NEXT(ADDRESS);
    }

    template <evmc_revision Rev>
    void balance(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<BALANCE, Rev>(
            runtime::balance<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(BALANCE);
    }

    template <evmc_revision Rev>
    void origin(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<ORIGIN, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.tx_origin));

        NEXT(ORIGIN);
    }

    template <evmc_revision Rev>
    void caller(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLER, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_address(ctx.env.sender));

        NEXT(CALLER);
    }

    template <evmc_revision Rev>
    void callvalue(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLVALUE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, runtime::uint256_from_bytes32(ctx.env.value));

        NEXT(CALLVALUE);
    }

    template <evmc_revision Rev>
    void calldataload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CALLDATALOAD, Rev>(
            runtime::calldataload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CALLDATALOAD);
    }

    template <evmc_revision Rev>
    void calldatasize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CALLDATASIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.input_data_size);

        NEXT(CALLDATASIZE);
    }

    template <evmc_revision Rev>
    void calldatacopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CALLDATACOPY, Rev>(
            runtime::calldatacopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CALLDATACOPY);
    }

    template <evmc_revision Rev>
    void codesize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CODESIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.code_size);

        NEXT(CODESIZE);
    }

    template <evmc_revision Rev>
    void codecopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CODECOPY, Rev>(
            runtime::codecopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CODECOPY);
    }

    template <evmc_revision Rev>
    void gasprice(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GAS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));

        NEXT(GAS);
    }

    template <evmc_revision Rev>
    void extcodesize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<EXTCODESIZE, Rev>(
            runtime::extcodesize<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(EXTCODESIZE);
    }

    template <evmc_revision Rev>
    void extcodecopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<EXTCODECOPY, Rev>(
            runtime::extcodecopy<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(EXTCODECOPY);
    }

    template <evmc_revision Rev>
    void returndatasize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<RETURNDATASIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.return_data_size);

        NEXT(RETURNDATASIZE);
    }

    template <evmc_revision Rev>
    void returndatacopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<RETURNDATACOPY, Rev>(
            runtime::returndatacopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(RETURNDATACOPY);
    }

    template <evmc_revision Rev>
    void extcodehash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<EXTCODEHASH, Rev>(
            runtime::extcodehash<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(EXTCODEHASH);
    }

    template <evmc_revision Rev>
    void blockhash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<BLOCKHASH, Rev>(
            runtime::blockhash,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(BLOCKHASH);
    }

    template <evmc_revision Rev>
    void coinbase(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<COINBASE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));

        NEXT(COINBASE);
    }

    template <evmc_revision Rev>
    void timestamp(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<TIMESTAMP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_timestamp);

        NEXT(TIMESTAMP);
    }

    template <evmc_revision Rev>
    void number(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<NUMBER, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_number);

        NEXT(NUMBER);
    }

    template <evmc_revision Rev>
    void prevrandao(
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

        NEXT(DIFFICULTY);
    }

    template <evmc_revision Rev>
    void gaslimit(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GASLIMIT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.env.tx_context.block_gas_limit);

        NEXT(GASLIMIT);
    }

    template <evmc_revision Rev>
    void chainid(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<CHAINID, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));

        NEXT(CHAINID);
    }

    template <evmc_revision Rev>
    void selfbalance(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SELFBALANCE, Rev>(
            runtime::selfbalance,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SELFBALANCE);
    }

    template <evmc_revision Rev>
    void basefee(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BASEFEE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));

        NEXT(BASEFEE);
    }

    template <evmc_revision Rev>
    void blobhash(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<BLOBHASH, Rev>(
            runtime::blobhash,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(BLOBHASH);
    }

    template <evmc_revision Rev>
    void blobbasefee(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<BLOBBASEFEE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(
            stack_top,
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));

        NEXT(BLOBBASEFEE);
    }

    // Memory & Storage
    template <evmc_revision Rev>
    void mload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MLOAD, Rev>(
            runtime::mload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MLOAD);
    }

    template <evmc_revision Rev>
    void mstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MSTORE, Rev>(
            runtime::mstore,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MSTORE);
    }

    template <evmc_revision Rev>
    void mstore8(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MSTORE8, Rev>(
            runtime::mstore8,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MSTORE8);
    }

    template <evmc_revision Rev>
    void mcopy(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<MCOPY, Rev>(
            runtime::mcopy,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(MCOPY);
    }

    template <evmc_revision Rev>
    void sstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SSTORE, Rev>(
            runtime::sstore<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SSTORE);
    }

    template <evmc_revision Rev>
    void sload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<SLOAD, Rev>(
            runtime::sload<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(SLOAD);
    }

    template <evmc_revision Rev>
    void tstore(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<TSTORE, Rev>(
            runtime::tstore,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(TSTORE);
    }

    template <evmc_revision Rev>
    void tload(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<TLOAD, Rev>(
            runtime::tload,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(TLOAD);
    }

    // Execution Intercode
    template <evmc_revision Rev>
    void
    pc(runtime::Context &ctx, Intercode const &analysis,
       utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
       std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<PC, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, instr_ptr - analysis.code());

        NEXT(PC);
    }

    template <evmc_revision Rev>
    void msize(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<MSIZE, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, ctx.memory.size);

        NEXT(MSIZE);
    }

    template <evmc_revision Rev>
    void
    gas(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<GAS, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        push(stack_top, gas_remaining);

        NEXT(GAS);
    }

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    void push(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        push_impl<N, Rev>::push(
            ctx, analysis, stack_bottom, stack_top, gas_remaining, instr_ptr);

        NEXT_PUSH(PUSH0 + N);
    }

    template <evmc_revision Rev>
    void
    pop(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<POP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        NEXT(POP);
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void
    dup(runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<DUP1 + (N - 1), Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);

        auto const old_top = stack_top;
        push(stack_top, *(old_top - (N - 1)));

        NEXT(DUP1 + (N - 1));
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void swap(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        check_requirements<SWAP1 + (N - 1), Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);

        auto const top = stack_top->to_avx();
        *stack_top = *(stack_top - N);
        *(stack_top - N) = utils::uint256_t{top};

        NEXT(SWAP1 + (N - 1));
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
    void jump(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        check_requirements<JUMP, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        auto const &target = pop(stack_top);
        auto const new_ip = jump_impl(ctx, analysis, target);

        MUSTTAIL return instruction_table<Rev>[*new_ip](
            ctx, analysis, stack_bottom, stack_top, gas_remaining, new_ip);
    }

    template <evmc_revision Rev>
    void jumpi(
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

            MUSTTAIL return instruction_table<Rev>[*new_ip](
                ctx, analysis, stack_bottom, stack_top, gas_remaining, new_ip);
        }
        else {
            ++instr_ptr;
            MUSTTAIL return instruction_table<Rev>[*instr_ptr](
                ctx,
                analysis,
                stack_bottom,
                stack_top,
                gas_remaining,
                instr_ptr);
        }
    }

    template <evmc_revision Rev>
    void jumpdest(
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

        NEXT(JUMPDEST);
    }

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    void
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

        checked_runtime_call<LOG0 + N, Rev>(
            std::get<N>(impls),
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(LOG0 + N);
    }

    // Call & Create
    template <evmc_revision Rev>
    void create(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CREATE, Rev>(
            runtime::create<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CREATE);
    }

    template <evmc_revision Rev>
    void call(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CALL, Rev>(
            runtime::call<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CALL);
    }

    template <evmc_revision Rev>
    void callcode(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CALLCODE, Rev>(
            runtime::callcode<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CALLCODE);
    }

    template <evmc_revision Rev>
    void delegatecall(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<DELEGATECALL, Rev>(
            runtime::delegatecall<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(DELEGATECALL);
    }

    template <evmc_revision Rev>
    void create2(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<CREATE2, Rev>(
            runtime::create2<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(CREATE2);
    }

    template <evmc_revision Rev>
    void staticcall(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        checked_runtime_call<STATICCALL, Rev>(
            runtime::staticcall<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);

        NEXT(STATICCALL);
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
    void return_(
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
    void revert(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        check_requirements<REVERT, Rev>(
            ctx, analysis, stack_bottom, stack_top, gas_remaining);
        return_impl(Revert, ctx, stack_top, gas_remaining);
    }

    template <evmc_revision Rev>
    void selfdestruct(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        fuzz_tstore_stack(ctx, stack_bottom, stack_top, analysis.code_size());
        checked_runtime_call<SELFDESTRUCT, Rev>(
            runtime::selfdestruct<Rev>,
            ctx,
            analysis,
            stack_bottom,
            stack_top,
            gas_remaining,
            instr_ptr);
    }

    inline void stop(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *)
    {
        fuzz_tstore_stack(ctx, stack_bottom, stack_top, analysis.code_size());
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Success);
    }

    inline void invalid(
        runtime::Context &ctx, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t gas_remaining, std::uint8_t const *)
    {
        ctx.gas_remaining = gas_remaining;
        ctx.exit(Error);
    }
}

#undef MUSTTAIL
#undef NEXT
#undef NEXT_PUSH
