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
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        static constexpr auto info = compiler::opcode_table<Rev>[Instr];

        if constexpr (info.min_gas > 0) {
            ctx.gas_remaining -= info.min_gas;

            if (MONAD_COMPILER_UNLIKELY(ctx.gas_remaining < 0)) {
                ctx.exit(Error);
            }
        }

        if constexpr (info.min_stack == 0 && !info.increases_stack) {
            return;
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
    }

    // Arithmetic
    template <evmc_revision Rev>
    void
    add(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<ADD, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = runtime::unrolled_add(a, b);
        state.next();
    }

    template <evmc_revision Rev>
    void
    mul(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MUL, Rev>(ctx, state, stack_bottom);
        call_runtime(monad_runtime_mul, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void
    sub(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SUB, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a - b;
        state.next();
    }

    template <evmc_revision Rev>
    void udiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<DIV, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::udiv, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void sdiv(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<DIV, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::sdiv, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void umod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MOD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::umod, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void smod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SMOD, Rev>(ctx, state, stack_bottom);

        call_runtime(runtime::smod, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void addmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<ADDMOD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::addmod, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void mulmod(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MULMOD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::mulmod, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void
    exp(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<EXP, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::exp<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void signextend(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SIGNEXTEND, Rev>(ctx, state, stack_bottom);
        auto &&[b, x] = state.pop_for_overwrite();
        x = monad::utils::signextend(b, x);
        state.next();
    }

    // Boolean
    template <evmc_revision Rev>
    void
    lt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom)
    {
        check_requirements<LT, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a < b;
        state.next();
    }

    template <evmc_revision Rev>
    void
    gt(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom)
    {
        check_requirements<GT, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a > b;
        state.next();
    }

    template <evmc_revision Rev>
    void
    slt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SLT, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = intx::slt(a, b);
        state.next();
    }

    template <evmc_revision Rev>
    void
    sgt(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SGT, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = intx::slt(b, a); // note swapped arguments
        state.next();
    }

    template <evmc_revision Rev>
    void
    eq(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom)
    {
        check_requirements<EQ, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = (a == b);
        state.next();
    }

    template <evmc_revision Rev>
    void iszero(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<ISZERO, Rev>(ctx, state, stack_bottom);
        auto &a = state.top();
        a = (a == 0);
        state.next();
    }

    // Bitwise
    template <evmc_revision Rev>
    void and_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<AND, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a & b;
        state.next();
    }

    template <evmc_revision Rev>
    void
    or_(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<OR, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a | b;
        state.next();
    }

    template <evmc_revision Rev>
    void xor_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<XOR, Rev>(ctx, state, stack_bottom);
        auto &&[a, b] = state.pop_for_overwrite();
        b = a ^ b;
        state.next();
    }

    template <evmc_revision Rev>
    void not_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<NOT, Rev>(ctx, state, stack_bottom);
        auto &a = state.top();
        a = ~a;
        state.next();
    }

    template <evmc_revision Rev>
    void byte(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BYTE, Rev>(ctx, state, stack_bottom);
        auto &&[i, x] = state.pop_for_overwrite();
        x = utils::byte(i, x);
        state.next();
    }

    template <evmc_revision Rev>
    void
    shl(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SHL, Rev>(ctx, state, stack_bottom);
        auto &&[shift, value] = state.pop_for_overwrite();
        value <<= shift;
        state.next();
    }

    template <evmc_revision Rev>
    void
    shr(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SHR, Rev>(ctx, state, stack_bottom);
        auto &&[shift, value] = state.pop_for_overwrite();
        value >>= shift;
        state.next();
    }

    template <evmc_revision Rev>
    void
    sar(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SAR, Rev>(ctx, state, stack_bottom);
        auto &&[shift, value] = state.pop_for_overwrite();
        value = monad::utils::sar(shift, value);
        state.next();
    }

    // Data
    template <evmc_revision Rev>
    void sha3(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SHA3, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::sha3, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void address(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<ADDRESS, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_address(ctx.env.recipient));
        state.next();
    }

    template <evmc_revision Rev>
    void balance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BALANCE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::balance<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void origin(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<ORIGIN, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_address(ctx.env.tx_context.tx_origin));
        state.next();
    }

    template <evmc_revision Rev>
    void caller(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLER, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_address(ctx.env.sender));
        state.next();
    }

    template <evmc_revision Rev>
    void callvalue(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLVALUE, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_bytes32(ctx.env.value));
        state.next();
    }

    template <evmc_revision Rev>
    void calldataload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLDATALOAD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::calldataload, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void calldatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLDATASIZE, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.input_data_size);
        state.next();
    }

    template <evmc_revision Rev>
    void calldatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLDATACOPY, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::calldatacopy, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void codesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CODESIZE, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.code_size);
        state.next();
    }

    template <evmc_revision Rev>
    void codecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CODECOPY, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::codecopy, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void gasprice(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<GAS, Rev>(ctx, state, stack_bottom);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.tx_gas_price));
        state.next();
    }

    template <evmc_revision Rev>
    void extcodesize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<EXTCODESIZE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::extcodesize<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void extcodecopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<EXTCODECOPY, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::extcodecopy<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void returndatasize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<RETURNDATASIZE, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.return_data_size);
        state.next();
    }

    template <evmc_revision Rev>
    void returndatacopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<RETURNDATACOPY, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::returndatacopy, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void extcodehash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<EXTCODEHASH, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::extcodehash<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void blockhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BLOCKHASH, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::blockhash, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void coinbase(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<COINBASE, Rev>(ctx, state, stack_bottom);
        state.push(
            runtime::uint256_from_address(ctx.env.tx_context.block_coinbase));
        state.next();
    }

    template <evmc_revision Rev>
    void timestamp(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<TIMESTAMP, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.tx_context.block_timestamp);
        state.next();
    }

    template <evmc_revision Rev>
    void number(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<NUMBER, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.tx_context.block_number);
        state.next();
    }

    template <evmc_revision Rev>
    void prevrandao(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<DIFFICULTY, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_bytes32(
            ctx.env.tx_context.block_prev_randao));
        state.next();
    }

    template <evmc_revision Rev>
    void gaslimit(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<GASLIMIT, Rev>(ctx, state, stack_bottom);
        state.push(ctx.env.tx_context.block_gas_limit);
        state.next();
    }

    template <evmc_revision Rev>
    void chainid(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CHAINID, Rev>(ctx, state, stack_bottom);
        state.push(runtime::uint256_from_bytes32(ctx.env.tx_context.chain_id));
        state.next();
    }

    template <evmc_revision Rev>
    void selfbalance(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SELFBALANCE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::selfbalance, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void basefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BASEFEE, Rev>(ctx, state, stack_bottom);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.block_base_fee));
        state.next();
    }

    template <evmc_revision Rev>
    void blobhash(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BLOBHASH, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::blobhash, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void blobbasefee(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<BLOBBASEFEE, Rev>(ctx, state, stack_bottom);
        state.push(
            runtime::uint256_from_bytes32(ctx.env.tx_context.blob_base_fee));
        state.next();
    }

    // Memory & Storage
    template <evmc_revision Rev>
    void mload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MLOAD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::mload, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void mstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MSTORE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::mstore, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void mstore8(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MSTORE8, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::mstore8, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void mcopy(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MCOPY, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::mcopy, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void tload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<TLOAD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::tload, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void sstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SSTORE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::sstore<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void sload(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SLOAD, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::sload<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void tstore(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<TSTORE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::tstore, ctx, state);
        state.next();
    }

    // Execution State
    template <evmc_revision Rev>
    void
    pc(runtime::Context &ctx, State &state,
       utils::uint256_t const *stack_bottom)
    {
        check_requirements<PC, Rev>(ctx, state, stack_bottom);
        state.push(state.instr_ptr - state.analysis.code());
        state.next();
    }

    template <evmc_revision Rev>
    void msize(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<MSIZE, Rev>(ctx, state, stack_bottom);
        state.push(ctx.memory.size);
        state.next();
    }

    template <evmc_revision Rev>
    void
    gas(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<GAS, Rev>(ctx, state, stack_bottom);
        state.push(ctx.gas_remaining);
        state.next();
    }

    // Stack
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    void push(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        using subword_t = utils::uint256_t::word_type;

        // We need to do this memcpy dance to avoid triggering UB when reading
        // whole words from potentially unaligned addresses in the instruction
        // stream. The compilers seem able to optimise this out effectively, and
        // the generated code doesn't appear different to the UB-triggering
        // version.
        constexpr auto read_unaligned = [](std::uint8_t const *ptr) {
            alignas(subword_t) std::uint8_t aligned_mem[sizeof(subword_t)];
            std::memcpy(&aligned_mem[0], ptr, sizeof(subword_t));
            return std::byteswap(
                *reinterpret_cast<subword_t *>(&aligned_mem[0]));
        };

        static constexpr auto whole_words = N / 8;
        static constexpr auto leading_part = N % 8;

        check_requirements<PUSH0 + N, Rev>(ctx, state, stack_bottom);

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
    }

    template <evmc_revision Rev>
    void
    pop(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<POP, Rev>(ctx, state, stack_bottom);
        --state.stack_top;
        state.next();
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void
    dup(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<DUP1 + (N - 1), Rev>(ctx, state, stack_bottom);
        state.push(*(state.stack_top - (N - 1)));
        state.next();
    }

    template <std::size_t N, evmc_revision Rev>
        requires(N >= 1)
    void swap(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SWAP1 + (N - 1), Rev>(ctx, state, stack_bottom);
        std::swap(state.top(), *(state.stack_top - N));
        state.next();
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
    void jump(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<JUMP, Rev>(ctx, state, stack_bottom);
        auto const &target = state.pop();
        jump_impl(ctx, state, target);
    }

    template <evmc_revision Rev>
    void jumpi(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<JUMPI, Rev>(ctx, state, stack_bottom);
        auto const &target = state.pop();
        auto const &cond = state.pop();

        if (cond) {
            jump_impl(ctx, state, target);
        }
        else {
            state.next();
        }
    }

    template <evmc_revision Rev>
    void jumpdest(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<JUMPDEST, Rev>(ctx, state, stack_bottom);
        state.next();
    }

    // Logging
    template <std::size_t N, evmc_revision Rev>
        requires(N <= 4)
    void
    log(runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<LOG0 + N, Rev>(ctx, state, stack_bottom);
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
    void create(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CREATE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::create<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void call(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALL, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::call<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void callcode(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CALLCODE, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::callcode<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void delegatecall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<DELEGATECALL, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::delegatecall<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void create2(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<CREATE2, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::create2<Rev>, ctx, state);
        state.next();
    }

    template <evmc_revision Rev>
    void staticcall(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<STATICCALL, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::staticcall<Rev>, ctx, state);
        state.next();
    }

    // VM Control
    namespace
    {
        inline void return_impl(
            runtime::StatusCode const code, runtime::Context &ctx, State &state)
        {
            for (auto *result_loc : {&ctx.result.offset, &ctx.result.size}) {
                std::copy_n(
                    intx::as_bytes(state.pop()),
                    32,
                    reinterpret_cast<std::uint8_t *>(result_loc));
            }

            ctx.exit(code);
        }
    }

    template <evmc_revision Rev>
    void return_(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<RETURN, Rev>(ctx, state, stack_bottom);
        return_impl(Success, ctx, state);
    }

    template <evmc_revision Rev>
    void revert(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<REVERT, Rev>(ctx, state, stack_bottom);
        return_impl(Revert, ctx, state);
    }

    template <evmc_revision Rev>
    void selfdestruct(
        runtime::Context &ctx, State &state,
        utils::uint256_t const *stack_bottom)
    {
        check_requirements<SELFDESTRUCT, Rev>(ctx, state, stack_bottom);
        call_runtime(runtime::selfdestruct<Rev>, ctx, state);
    }

    inline void stop(runtime::Context &ctx, State &, utils::uint256_t const *)
    {
        ctx.exit(Success);
    }

    inline void
    invalid(runtime::Context &ctx, State &, utils::uint256_t const *)
    {
        ctx.exit(Error);
    }
}
