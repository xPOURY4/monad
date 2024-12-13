#include "asmjit/core/jitruntime.h"
#include "compiler/evm_opcodes.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/x86/virtual_stack.h"
#include "compiler/types.h"
#include "evmc/evmc.h"
#include "evmc/evmc.hpp"
#include "intx/intx.hpp"
#include "runtime/math.h"
#include "runtime/types.h"
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace runtime = monad::runtime;
using namespace monad::compiler;
using namespace monad::compiler::native;
using namespace intx;

namespace
{
    evmc::address max_address()
    {
        evmc::address ret;
        std::memset(ret.bytes, -1, sizeof(ret.bytes) / sizeof(*ret.bytes));
        return ret;
    }

    evmc::bytes32 max_bytes32()
    {
        evmc::bytes32 ret;
        std::memset(ret.bytes, -1, sizeof(ret.bytes) / sizeof(*ret.bytes));
        return ret;
    }

    runtime::Result test_result()
    {
        runtime::Result ret;
        ret.status = static_cast<runtime::StatusCode>(
            std::numeric_limits<uint64_t>::max());
        memcpy(ret.offset, max_bytes32().bytes, 32);
        memcpy(ret.size, max_bytes32().bytes, 32);
        return ret;
    }

    runtime::Context test_context(int64_t gas_remaining = 1'000'000)
    {
        return runtime::Context{
            .host = nullptr,
            .context = nullptr,
            .gas_remaining = gas_remaining,
            .gas_refund = 0,
            .env =
                {
                    .evmc_flags = 0,
                    .depth = 0,
                    .recipient = max_address(),
                    .sender = max_address(),
                    .value = max_bytes32(),
                    .create2_salt = max_bytes32(),
                    .input_data = {},
                    .code = {},
                    .return_data = {},
                    .input_data_size = 0,
                    .code_size = 0,
                    .return_data_size = 0,
                },
            .result = test_result(),
            .memory = {},
            .exit_stack_ptr = nullptr};
    }

    struct TestStackMemoryDeleter
    {
        void operator()(uint8_t *p) const
        {
            std::free(p);
        }
    } test_stack_memory_deleter;

    std::unique_ptr<uint8_t, TestStackMemoryDeleter> test_stack_memory()
    {
        return {
            reinterpret_cast<uint8_t *>(std::aligned_alloc(32, 32 * 1024)),
            test_stack_memory_deleter};
    }

    void mov_literal_to_location_type(
        Emitter &emit, int32_t stack_index, Emitter::LocationType loc)
    {
        Stack &stack = emit.get_stack();
        auto elem = stack.get(stack_index);
        ASSERT_TRUE(
            elem->literal() && !elem->stack_offset() && !elem->avx_reg() &&
            !elem->general_reg());
        switch (loc) {
        case Emitter::LocationType::AvxReg:
            emit.mov_stack_index_to_avx_reg(stack_index);
            stack.spill_literal(elem);
            ASSERT_TRUE(
                elem->avx_reg() && !elem->stack_offset() && !elem->literal() &&
                !elem->general_reg());
            break;
        case Emitter::LocationType::GeneralReg:
            emit.mov_stack_index_to_general_reg(stack_index);
            stack.spill_literal(elem);
            ASSERT_TRUE(
                elem->general_reg() && !elem->stack_offset() &&
                !elem->literal() && !elem->avx_reg());
            break;
        case Emitter::LocationType::StackOffset:
            emit.mov_stack_index_to_stack_offset(stack_index);
            stack.spill_literal(elem);
            ASSERT_TRUE(
                elem->stack_offset() && !elem->general_reg() &&
                !elem->literal() && !elem->avx_reg());
            break;
        case Emitter::LocationType::Literal:
            break;
        }
    }

    void copy_stack_offset_to_location_type(
        Emitter &emit, int32_t stack_index, Emitter::LocationType loc)
    {
        Stack &stack = emit.get_stack();
        auto elem = stack.get(stack_index);
        ASSERT_TRUE(
            elem->stack_offset() && !elem->general_reg() && !elem->avx_reg() &&
            !elem->literal());
        switch (loc) {
        case Emitter::LocationType::AvxReg:
            emit.mov_stack_index_to_avx_reg(stack_index);
            ASSERT_TRUE(
                elem->avx_reg() && elem->stack_offset() && !elem->literal() &&
                !elem->general_reg());
            break;
        case Emitter::LocationType::GeneralReg:
            emit.mov_stack_index_to_general_reg(stack_index);
            ASSERT_TRUE(
                elem->general_reg() && elem->stack_offset() &&
                !elem->literal() && !elem->avx_reg());
            break;
        case Emitter::LocationType::StackOffset:
            break;
        case Emitter::LocationType::Literal:
            ASSERT_TRUE(false);
            break;
        }
    }

    using PureEmitterInstr = std::function<void(Emitter &)>;
    using PureEmitterInstrPtr = void (Emitter::*)();

    void pure_bin_instr_test_instance(
        PureEmitterInstr instr, uint256_t const &left,
        Emitter::LocationType left_loc, uint256_t const &right,
        Emitter::LocationType right_loc, uint256_t const &result,
        basic_blocks::BasicBlocksIR const &ir, bool dup)
    {
#if 0
        if (left_loc != Emitter::LocationType::Literal || right_loc != Emitter::LocationType::Literal || dup) {
            return;
        }
#endif
#if 0
        std::cout <<
            std::format("LEFT {} : {}  and  RIGHT {} : {}",
                    left, Emitter::location_type_to_string(left_loc),
                    right, Emitter::location_type_to_string(right_loc)) << std::endl;
#endif

        asmjit::JitRuntime rt;

        Emitter emit{rt, ir.codesize};
        (void)emit.begin_new_block(ir.blocks()[0]);
        emit.push(right);
        if (dup) {
            emit.dup(1);
        }
        emit.push(left);
        if (dup) {
            emit.dup(1);
            emit.swap(2);
            emit.swap(1);
        }

        mov_literal_to_location_type(emit, 1 + 2 * dup, left_loc);
        mov_literal_to_location_type(emit, 2 * dup, right_loc);

        instr(emit);

        if (dup) {
            emit.swap(2);
            emit.swap(1);
            instr(emit);
        }
        else {
            emit.push(0);
        }
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ctx = test_context();
        auto const &ret = ctx.result;

        auto stack_memory = test_stack_memory();
        entry(&ctx, stack_memory.get());

        ASSERT_EQ(ret.status, runtime::StatusCode::Success);
        if (dup) {
            ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), result);
        }
        else {
            ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0);
        }
        ASSERT_EQ(intx::le::load<uint256_t>(ret.size), result);
    }

    void pure_una_instr_test_instance(
        PureEmitterInstr instr, uint256_t const &input,
        Emitter::LocationType loc, uint256_t const &result,
        basic_blocks::BasicBlocksIR const &ir, bool dup)
    {
#if 0
        if (loc != Emitter::LocationType::GeneralReg || dup) {
            return;
        }
#endif
#if 0
        std::cout <<
            std::format("INPUT {} : {} with dup = {}",
                    input, Emitter::location_type_to_string(loc), dup)
            << std::endl;
#endif

        asmjit::JitRuntime rt;

        Emitter emit{rt, ir.codesize};
        (void)emit.begin_new_block(ir.blocks()[0]);
        emit.push(input);
        if (dup) {
            emit.dup(1);
        }

        mov_literal_to_location_type(emit, dup, loc);

        instr(emit);

        if (dup) {
            emit.swap(1);
            instr(emit);
        }
        else {
            emit.push(0);
        }
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ctx = test_context();
        auto const &ret = ctx.result;

        auto stack_memory = test_stack_memory();
        entry(&ctx, stack_memory.get());

        ASSERT_EQ(ret.status, runtime::StatusCode::Success);
        if (dup) {
            ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), result);
        }
        else {
            ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0);
        }
        ASSERT_EQ(intx::le::load<uint256_t>(ret.size), result);
    }

    void pure_bin_instr_test(
        EvmOpCode opcode, PureEmitterInstr instr, uint256_t const &left,
        uint256_t const &right, uint256_t const &result)
    {
        std::vector<Emitter::LocationType> const locs = {
            Emitter::LocationType::Literal,
            Emitter::LocationType::AvxReg,
            Emitter::LocationType::GeneralReg,
            Emitter::LocationType::StackOffset};

        std::vector<uint8_t> bytecode1{PUSH0, PUSH0, opcode, PUSH0, RETURN};
        auto ir1 = basic_blocks::BasicBlocksIR(std::move(bytecode1));
        for (auto left_loc : locs) {
            for (auto right_loc : locs) {
                pure_bin_instr_test_instance(
                    instr,
                    left,
                    left_loc,
                    right,
                    right_loc,
                    result,
                    ir1,
                    false);
            }
        }

        std::vector<uint8_t> bytecode2{
            PUSH0,
            PUSH0,
            DUP1,
            PUSH0,
            DUP1,
            SWAP2,
            SWAP1,
            opcode,
            POP,
            opcode,
            RETURN};
        auto ir2 = basic_blocks::BasicBlocksIR(std::move(bytecode2));
        for (auto left_loc : locs) {
            for (auto right_loc : locs) {
                pure_bin_instr_test_instance(
                    instr, left, left_loc, right, right_loc, result, ir2, true);
            }
        }
    }

    void pure_bin_instr_test(
        EvmOpCode opcode, PureEmitterInstrPtr instr, uint256_t const &left,
        uint256_t const &right, uint256_t const &result)
    {
        pure_bin_instr_test(
            opcode, [&](Emitter &e) { (e.*instr)(); }, left, right, result);
    }

    void pure_una_instr_test(
        EvmOpCode opcode, PureEmitterInstr instr, uint256_t const &input,
        uint256_t const &result)
    {
        std::vector<Emitter::LocationType> const locs = {
            Emitter::LocationType::Literal,
            Emitter::LocationType::AvxReg,
            Emitter::LocationType::GeneralReg,
            Emitter::LocationType::StackOffset};

        std::vector<uint8_t> bytecode1{PUSH0, opcode, PUSH0, RETURN};
        auto ir1 = basic_blocks::BasicBlocksIR(std::move(bytecode1));
        for (auto loc : locs) {
            pure_una_instr_test_instance(instr, input, loc, result, ir1, false);
        }

        std::vector<uint8_t> bytecode2{
            PUSH0, DUP1, opcode, SWAP1, opcode, RETURN};
        auto ir2 = basic_blocks::BasicBlocksIR(std::move(bytecode2));
        for (auto loc : locs) {
            pure_una_instr_test_instance(instr, input, loc, result, ir1, true);
        }
    }

    void pure_una_instr_test(
        EvmOpCode opcode, PureEmitterInstrPtr instr, uint256_t const &input,
        uint256_t const &result)
    {
        pure_una_instr_test(
            opcode, [&](Emitter &e) { (e.*instr)(); }, input, result);
    }

    void jump_test(
        Emitter::LocationType loc1, Emitter::LocationType loc2,
        Emitter::LocationType loc_dest, bool swap)
    {
#if 0
        if (swap || loc1 != Emitter::LocationType::Literal || loc2 != Emitter::LocationType::AvxReg || loc_dest != Emitter::LocationType::Literal) {
            return;
        }
#endif
#if 0
        std::cout <<
            std::format("LOC1 {}  and  LOC2 {}  and  LOC_DEST {}",
                    Emitter::location_type_to_string(loc1),
                    Emitter::location_type_to_string(loc2),
                    Emitter::location_type_to_string(loc_dest)) << std::endl;
#endif

        auto ir =
            swap ? basic_blocks::BasicBlocksIR(
                       {PUSH0, PUSH0, PUSH0, SWAP1, JUMP, JUMPDEST, RETURN})
                 : basic_blocks::BasicBlocksIR(
                       {PUSH0, PUSH0, PUSH0, JUMP, JUMPDEST, RETURN});

        asmjit::JitRuntime rt;
        Emitter emit{rt, ir.codesize};

        for (auto const &[k, _] : ir.jump_dests()) {
            emit.add_jump_dest(k);
        }

        (void)emit.begin_new_block(ir.blocks()[0]);
        emit.push(1);
        mov_literal_to_location_type(emit, 0, loc1);
        if (swap) {
            emit.push(5);
            mov_literal_to_location_type(emit, 1, loc_dest);
            emit.push(2);
            mov_literal_to_location_type(emit, 2, loc2);
            emit.swap(1);
        }
        else {
            emit.push(2);
            mov_literal_to_location_type(emit, 1, loc2);
            emit.push(4);
            mov_literal_to_location_type(emit, 2, loc_dest);
        }
        emit.jump();
        (void)emit.begin_new_block(ir.blocks()[1]);
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ctx = test_context();
        auto const &ret = ctx.result;

        auto stack_memory = test_stack_memory();
        entry(&ctx, stack_memory.get());

        ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
        ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
    }

    basic_blocks::BasicBlocksIR
    get_jumpi_ir(bool deferred_comparison, bool swap, bool dup)
    {
        if (deferred_comparison && swap) {
            if (dup) {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     DUP2,
                     ISZERO,
                     DUP2,
                     SWAP2,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
            else {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     PUSH0,
                     ISZERO,
                     PUSH0,
                     SWAP2,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
        }
        if (deferred_comparison) {
            if (dup) {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     DUP2,
                     ISZERO,
                     DUP2,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
            else {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     PUSH0,
                     ISZERO,
                     PUSH0,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
        }
        if (swap) {
            if (dup) {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     DUP2,
                     DUP2,
                     SWAP2,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
            else {
                return basic_blocks::BasicBlocksIR(
                    {PUSH0,
                     PUSH0,
                     PUSH0,
                     PUSH0,
                     SWAP2,
                     JUMPI,
                     RETURN,
                     JUMPDEST,
                     REVERT});
            }
        }
        if (dup) {
            return basic_blocks::BasicBlocksIR(
                {PUSH0, PUSH0, DUP2, DUP2, JUMPI, RETURN, JUMPDEST, REVERT});
        }
        else {
            return basic_blocks::BasicBlocksIR(
                {PUSH0, PUSH0, PUSH0, PUSH0, JUMPI, RETURN, JUMPDEST, REVERT});
        }
    }

    void jumpi_test(
        Emitter::LocationType loc1, Emitter::LocationType loc2,
        Emitter::LocationType loc_cond, Emitter::LocationType loc_dest,
        bool take_jump, bool deferred_comparison, bool swap, bool dup)
    {
#if 0
        if (!take_jump || !deferred_comparison || !swap || !dup || loc1 != Emitter::LocationType::Literal || loc2 != Emitter::LocationType::Literal || loc_cond != Emitter::LocationType::Literal || loc_dest != Emitter::LocationType::StackOffset) {
            return;
        }
#endif
#if 0
        std::cout <<
            std::format("LOC1 {}  and  LOC2 {}  and  LOC_COND {}  and  LOC_DEST {}",
                    Emitter::location_type_to_string(loc1),
                    Emitter::location_type_to_string(loc2),
                    Emitter::location_type_to_string(loc_cond),
                    Emitter::location_type_to_string(loc_dest)) << std::endl;
#endif

        auto ir = get_jumpi_ir(deferred_comparison, swap, dup);

        asmjit::JitRuntime rt;
        Emitter emit{rt, ir.codesize};

        for (auto const &[k, _] : ir.jump_dests()) {
            emit.add_jump_dest(k);
        }

        uint256_t const cond = (take_jump + deferred_comparison) & 1;
        uint256_t const dest = 6 + swap + deferred_comparison;

        (void)emit.begin_new_block(ir.blocks()[0]);

        emit.push(cond);
        if (dup) {
            mov_literal_to_location_type(emit, 0, loc_cond);
        }
        else {
            mov_literal_to_location_type(emit, 0, loc1);
        }
        emit.push(dest);
        if (swap || dup) {
            mov_literal_to_location_type(emit, 1, loc_dest);
        }
        else {
            mov_literal_to_location_type(emit, 1, loc2);
        }
        if (dup) {
            emit.dup(2);
        }
        else {
            emit.push(cond);
            mov_literal_to_location_type(emit, 2, loc_cond);
        }
        if (deferred_comparison) {
            emit.iszero();
        }
        if (dup) {
            emit.dup(2);
        }
        else {
            emit.push(dest);
            if (swap) {
                mov_literal_to_location_type(emit, 3, loc2);
            }
            else {
                mov_literal_to_location_type(emit, 3, loc_dest);
            }
        }
        if (swap) {
            emit.swap(2);
        }
        emit.jumpi();

        (void)emit.begin_new_block(ir.blocks()[1]);
        emit.return_();

        (void)emit.begin_new_block(ir.blocks()[2]);
        emit.revert();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ctx = test_context();
        auto const &ret = ctx.result;

        auto stack_memory = test_stack_memory();
        entry(&ctx, stack_memory.get());

        if (take_jump) {
            ASSERT_EQ(ret.status, runtime::StatusCode::Revert);
        }
        else {
            ASSERT_EQ(ret.status, runtime::StatusCode::Success);
        }
        ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), dest);
        ASSERT_EQ(intx::le::load<uint256_t>(ret.size), cond);
    }

    void block_epilogue_test(
        Emitter::LocationType loc1, Emitter::LocationType loc2,
        Emitter::LocationType loc3, Emitter::LocationType loc4,
        Emitter::LocationType loc5)
    {
#if 0
        if (loc1 != Emitter::LocationType::StackOffset || loc2 != Emitter::LocationType::StackOffset || loc3 != Emitter::LocationType::StackOffset || loc4 != Emitter::LocationType::StackOffset || loc5 != Emitter::LocationType::StackOffset) {
            return;
        }
#endif
#if 0
        std::cout <<
            std::format("LOC1 {}  and  LOC2 {}  and  LOC3 {}  and  LOC4 {}  and  LOC5 {}",
                    Emitter::location_type_to_string(loc1),
                    Emitter::location_type_to_string(loc2),
                    Emitter::location_type_to_string(loc3),
                    Emitter::location_type_to_string(loc4),
                    Emitter::location_type_to_string(loc5)) << std::endl;
#endif

        auto ir = basic_blocks::BasicBlocksIR(
            {PUSH0,
             PUSH0,
             JUMPDEST,
             PUSH0,
             SWAP2,
             SWAP1,
             DUP1,
             DUP1,
             PUSH0,
             PUSH0,
             SWAP1,
             JUMPDEST,
             SUB,
             SUB,
             SUB,
             SUB,
             SUB,
             RETURN});

        asmjit::JitRuntime rt;
        Emitter emit{rt, ir.codesize};

        (void)emit.begin_new_block(ir.blocks()[0]);
        emit.push(1);
        emit.push(10);
        emit.fallthrough();

        (void)emit.begin_new_block(ir.blocks()[1]);
        copy_stack_offset_to_location_type(emit, -2, loc1);
        copy_stack_offset_to_location_type(emit, -1, loc2);

        emit.push(2); // [2, 10, 1]
        mov_literal_to_location_type(emit, 0, loc3);
        emit.swap(2); // [1, 10, 2]
        emit.swap(1); // [10, 1, 2]
        emit.dup(1); // [10, 10, 1, 2]
        emit.dup(1); // [10, 10 10, 1, 2]
        emit.push(1000); // [1000, 10, 10 10, 1, 2]
        mov_literal_to_location_type(emit, 3, loc4);
        emit.push(100); // [100, 1000, 10, 10 10, 1, 2]
        mov_literal_to_location_type(emit, 4, loc5);
        emit.swap(1); // [1000, 100, 10, 10 10, 1, 2]
        emit.fallthrough();

        (void)emit.begin_new_block(ir.blocks()[2]);
        emit.sub(); // [900, 10, 10 10, 1, 2]
        emit.sub(); // [890, 10 10, 1, 2]
        emit.sub(); // [880, 10, 1, 2]
        emit.sub(); // [870, 1, 2]
        emit.sub(); // [869, 2]
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ctx = test_context();
        auto const &ret = ctx.result;

        auto stack_memory = test_stack_memory();
        entry(&ctx, stack_memory.get());

        ASSERT_EQ(ret.status, runtime::StatusCode::Success);
        ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 869);
        ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 2);
    }

    void runtime_test_12_arg_fun(
        runtime::Context *ctx, uint256_t *result, uint256_t const *a,
        uint256_t const *b, uint256_t const *c, uint256_t const *d,
        uint256_t const *e, uint256_t const *f, uint256_t const *g,
        uint256_t const *h, uint256_t const *i, int64_t remaining_base_gas)
    {
        *result = uint256_t{ctx->gas_remaining} -
                  (uint256_t{remaining_base_gas} -
                   (*a - (*b - (*c - (*d - (*e - (*f - (*g - (*h - *i)))))))));
    }

    void runtime_test_11_arg_fun(
        runtime::Context *ctx, uint256_t *result, uint256_t const *a,
        uint256_t const *b, uint256_t const *c, uint256_t const *d,
        uint256_t const *e, uint256_t const *f, uint256_t const *g,
        uint256_t const *h, int64_t remaining_base_gas)
    {
        *result = uint256_t{ctx->gas_remaining} -
                  (uint256_t{remaining_base_gas} -
                   (*a - (*b - (*c - (*d - (*e - (*f - (*g - *h))))))));
    }
}

TEST(Emitter, empty)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(
        static_cast<uint64_t>(ret.status),
        std::numeric_limits<uint64_t>::max());
}

TEST(Emitter, stop)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 1};
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, invalid_instruction)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 1};
    emit.invalid_instruction();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::InvalidInstruction);
}

TEST(Emitter, gas_decrement_no_check_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};
    emit.gas_decrement_no_check(2);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(5);

    entry(&ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 3);
}

TEST(Emitter, gas_decrement_no_check_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};
    emit.gas_decrement_no_check(7);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(5);

    entry(&ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -2);
}

TEST(Emitter, gas_decrement_check_non_negative_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};
    emit.gas_decrement_check_non_negative(6);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(5);
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -1);
    ASSERT_EQ(ret.status, runtime::StatusCode::OutOfGas);
}

TEST(Emitter, gas_decrement_check_non_negative_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};
    emit.gas_decrement_check_non_negative(5);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(5);
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 0);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, gas_decrement_check_non_negative_3)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt, 0};
    emit.gas_decrement_check_non_negative(4);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(5);
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 1);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, return_)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    uint256_t const size_value = uint256_t{1} << 255;
    uint256_t const offset_value =
        std::numeric_limits<uint256_t>::max() - (uint256_t{1} << 31) + 1;
    emit.push(size_value);
    emit.push(offset_value);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), offset_value);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), size_value);
}

TEST(Emitter, revert)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    uint256_t const size_value = uint256_t{1} << 31;
    uint256_t const offset_value = (uint256_t{1} << 31) - 1;
    emit.push(size_value);
    emit.push(offset_value);
    emit.revert();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Revert);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), offset_value);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), size_value);
}

TEST(Emitter, mov_stack_index_to_avx_reg)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    Stack &stack = emit.get_stack();
    emit.push(1);
    emit.push(2);

    auto e0 = stack.get(0);

    emit.mov_stack_index_to_avx_reg(0); // literal -> avx reg
    stack.spill_literal(e0);
    ASSERT_TRUE(
        e0->avx_reg() && !e0->stack_offset() && !e0->literal() &&
        !e0->general_reg());

    emit.mov_stack_index_to_avx_reg(0); // avx reg -> avx reg
    ASSERT_TRUE(
        e0->avx_reg() && !e0->stack_offset() && !e0->literal() &&
        !e0->general_reg());

    emit.mov_stack_index_to_general_reg(0);
    stack.spill_stack_offset(e0);
    (void)stack.spill_avx_reg(e0);
    ASSERT_TRUE(
        e0->general_reg() && !e0->stack_offset() && !e0->literal() &&
        !e0->avx_reg());

    emit.mov_stack_index_to_avx_reg(0); // general reg -> stack offset & avx reg
    (void)stack.spill_general_reg(e0);
    (void)stack.spill_avx_reg(e0);
    ASSERT_TRUE(
        e0->stack_offset() && !e0->general_reg() && !e0->literal() &&
        !e0->avx_reg());

    emit.mov_stack_index_to_avx_reg(0); // stack offset -> avx reg
    stack.spill_stack_offset(e0);
    ASSERT_TRUE(
        e0->avx_reg() && !e0->general_reg() && !e0->literal() &&
        !e0->stack_offset());

    e0.reset();

    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, mov_stack_index_to_general_reg)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    Stack &stack = emit.get_stack();
    emit.push(1);
    emit.push(2);

    auto e1 = stack.get(1);

    emit.mov_stack_index_to_general_reg(1); // literal -> general reg
    stack.spill_literal(e1);
    ASSERT_TRUE(
        e1->general_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_general_reg(1); // general reg -> general reg
    ASSERT_TRUE(
        e1->general_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_avx_reg(1);
    stack.spill_stack_offset(e1);
    (void)stack.spill_general_reg(e1);
    ASSERT_TRUE(
        e1->avx_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_general_reg(
        1); // avx reg -> stack offset & general reg
    (void)stack.spill_avx_reg(e1);
    (void)stack.spill_general_reg(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->avx_reg() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_general_reg(1); // stack offset -> general reg
    stack.spill_stack_offset(e1);
    ASSERT_TRUE(
        e1->general_reg() && !e1->avx_reg() && !e1->literal() &&
        !e1->stack_offset());

    e1.reset();

    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, mov_stack_index_to_stack_offset)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    Stack &stack = emit.get_stack();
    emit.push(1);
    emit.push(2);

    auto e1 = stack.get(1);

    emit.mov_stack_index_to_stack_offset(1); // literal -> stack offset
    stack.spill_literal(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->general_reg() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_stack_offset(1); // stack offset -> stack offset
    stack.spill_literal(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->general_reg() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_avx_reg(1);
    stack.spill_stack_offset(e1);
    ASSERT_TRUE(
        e1->avx_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_stack_offset(1); // avx reg -> stack offset
    (void)stack.spill_avx_reg(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->avx_reg() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_general_reg(1); // stack offset -> general reg
    stack.spill_stack_offset(e1);
    ASSERT_TRUE(
        e1->general_reg() && !e1->avx_reg() && !e1->literal() &&
        !e1->stack_offset());

    emit.mov_stack_index_to_stack_offset(1); // general reg -> stack offset
    (void)stack.spill_general_reg(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->avx_reg() && !e1->literal() &&
        !e1->general_reg());

    e1.reset();

    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, discharge_deferred_comparison)
{
    auto ir = basic_blocks::BasicBlocksIR(
        {PUSH0, PUSH0, LT, DUP1, DUP1, PUSH0, SWAP1, POP, LT, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    Stack const &stack = emit.get_stack();
    emit.push(2);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::StackOffset);
    emit.push(1);
    ASSERT_FALSE(stack.has_deferred_comparison());
    emit.lt();
    ASSERT_TRUE(stack.has_deferred_comparison_at(0));
    emit.dup(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0));
    ASSERT_TRUE(stack.has_deferred_comparison_at(1));
    emit.dup(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0));
    ASSERT_TRUE(stack.has_deferred_comparison_at(1));
    ASSERT_TRUE(stack.has_deferred_comparison_at(2));
    emit.push(3);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0));
    ASSERT_TRUE(stack.has_deferred_comparison_at(1));
    ASSERT_TRUE(stack.has_deferred_comparison_at(2));
    ASSERT_FALSE(stack.has_deferred_comparison_at(3));
    emit.swap(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0));
    ASSERT_TRUE(stack.has_deferred_comparison_at(1));
    ASSERT_FALSE(stack.has_deferred_comparison_at(2));
    ASSERT_TRUE(stack.has_deferred_comparison_at(3));
    emit.pop();
    emit.lt();
    ASSERT_FALSE(stack.has_deferred_comparison_at(0));
    ASSERT_TRUE(stack.has_deferred_comparison_at(1));
    emit.return_();
    ASSERT_FALSE(stack.has_deferred_comparison());

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, discharge_negated_deferred_comparison)
{
    auto ir = basic_blocks::BasicBlocksIR(
        {PUSH0,
         PUSH0,
         LT,
         DUP1,
         ISZERO,
         SWAP1,
         DUP1,
         ISZERO,
         SWAP2,
         ISZERO,
         SWAP2,
         LT,
         SWAP1,
         ISZERO,
         ISZERO,
         ISZERO,
         RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    Stack const &stack = emit.get_stack();
    emit.push(2);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::StackOffset);
    emit.push(1);
    ASSERT_FALSE(stack.has_deferred_comparison());
    emit.lt();
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 1
    emit.dup(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    emit.iszero();
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 0
    emit.swap(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 0
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    emit.dup(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 0
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(2)); // 1
    emit.iszero();
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 0
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(2)); // 0
    emit.swap(2);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 0
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(2)); // 0
    emit.iszero();
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 0
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(2)); // 1
    emit.swap(2);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(2)); // 0
    emit.lt();
    ASSERT_FALSE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    emit.swap(1);
    ASSERT_TRUE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_FALSE(stack.has_deferred_comparison_at(1)); // 1
    emit.iszero();
    ASSERT_FALSE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 0
    emit.iszero();
    ASSERT_FALSE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 1
    emit.iszero();
    ASSERT_FALSE(stack.has_deferred_comparison_at(0)); // 1
    ASSERT_TRUE(stack.has_deferred_comparison_at(1)); // 0
    emit.return_();
    ASSERT_FALSE(stack.has_deferred_comparison());

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, lt)
{
    pure_bin_instr_test(LT, &Emitter::lt, 5, 6, 1);
    pure_bin_instr_test(LT, &Emitter::lt, -1, -1, 0);
    pure_bin_instr_test(
        LT,
        &Emitter::lt,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        0);
}

TEST(Emitter, gt)
{
    pure_bin_instr_test(GT, &Emitter::gt, 5, 6, 0);
    pure_bin_instr_test(GT, &Emitter::gt, -1, -1, 0);
    pure_bin_instr_test(
        GT,
        &Emitter::gt,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        1);
}

TEST(Emitter, slt)
{
    pure_bin_instr_test(SLT, &Emitter::slt, 5, 6, 1);
    pure_bin_instr_test(SLT, &Emitter::slt, -1, -1, 0);
    pure_bin_instr_test(
        SLT,
        &Emitter::slt,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        0);
    pure_bin_instr_test(
        SLT,
        &Emitter::slt,
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max(),
        1);
}

TEST(Emitter, sgt)
{
    pure_bin_instr_test(SGT, &Emitter::sgt, 5, 6, 0);
    pure_bin_instr_test(SGT, &Emitter::sgt, -1, -1, 0);
    pure_bin_instr_test(
        SGT,
        &Emitter::gt,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        1);
    pure_bin_instr_test(
        SGT,
        &Emitter::gt,
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max(),
        0);
}

TEST(Emitter, sub)
{
    pure_bin_instr_test(
        SUB, &Emitter::sub, 5, 6, std::numeric_limits<uint256_t>::max());
    pure_bin_instr_test(SUB, &Emitter::sub, -1, -1, 0);
    pure_bin_instr_test(
        SUB,
        &Emitter::sub,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        1);
    pure_bin_instr_test(
        SUB,
        &Emitter::sub,
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max());
}

TEST(Emitter, sub_identity)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SUB});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(10);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(1);
    emit.sub();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, add)
{
    pure_bin_instr_test(ADD, &Emitter::add, 5, 6, 11);
    pure_bin_instr_test(
        ADD,
        &Emitter::add,
        -1,
        -1,
        uint256_t{0, 1, 0, 0} + uint256_t{0, 1, 0, 0} - 2);
    pure_bin_instr_test(
        ADD,
        &Emitter::add,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max() - 2);
    pure_bin_instr_test(
        ADD,
        &Emitter::add,
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 2);
}

TEST(Emitter, add_identity_right)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, ADD});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(10);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(1);
    emit.add();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, add_identity_left)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, ADD});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(10);
    emit.push(0);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.add();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, and_)
{
    pure_bin_instr_test(AND, &Emitter::and_, 1, 3, 1);
    pure_bin_instr_test(AND, &Emitter::and_, 2, 1, 0);
    pure_bin_instr_test(
        AND,
        &Emitter::and_,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max() - 1);
}

TEST(Emitter, and_identity_left)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, AND});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(10);
    emit.push(std::numeric_limits<uint256_t>::max());
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::AvxReg);
    auto e = emit.get_stack().get(0);
    emit.and_();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, and_identity_right)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, AND});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(std::numeric_limits<uint256_t>::max());
    emit.push(10);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::AvxReg);
    auto e = emit.get_stack().get(1);
    emit.and_();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, or_)
{
    pure_bin_instr_test(OR, &Emitter::or_, 1, 3, 3);
    pure_bin_instr_test(OR, &Emitter::or_, 2, 1, 3);
    pure_bin_instr_test(
        OR,
        &Emitter::or_,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        std::numeric_limits<uint256_t>::max());
}

TEST(Emitter, or_identity_left)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, OR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(10);
    emit.push(0);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::AvxReg);
    auto e = emit.get_stack().get(0);
    emit.or_();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, or_identity_right)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, AND});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(10);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::AvxReg);
    auto e = emit.get_stack().get(1);
    emit.or_();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, xor_)
{
    pure_bin_instr_test(XOR, &Emitter::xor_, 1, 3, 2);
    pure_bin_instr_test(XOR, &Emitter::xor_, 2, 1, 3);
    pure_bin_instr_test(
        XOR,
        &Emitter::xor_,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        1);
}

TEST(Emitter, xor_same)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, DUP1, XOR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.dup(1);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::AvxReg);
    auto e0 = emit.get_stack().get(0);
    auto e1 = emit.get_stack().get(1);
    ASSERT_EQ(e0, e1);
    emit.xor_();
    ASSERT_EQ(emit.get_stack().get(0)->literal()->value, uint256_t{0});
}

TEST(Emitter, eq)
{
    pure_bin_instr_test(EQ, &Emitter::eq, 0, 0, 1);
    pure_bin_instr_test(EQ, &Emitter::eq, 1, 0, 0);
    pure_bin_instr_test(
        EQ,
        &Emitter::eq,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max() - 1,
        0);
    pure_bin_instr_test(
        EQ,
        &Emitter::eq,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max(),
        1);
}

TEST(Emitter, eq_same)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, DUP1, EQ});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.dup(1);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::AvxReg);
    auto e0 = emit.get_stack().get(0);
    auto e1 = emit.get_stack().get(1);
    ASSERT_EQ(e0, e1);
    emit.eq();
    ASSERT_EQ(emit.get_stack().get(0)->literal()->value, uint256_t{1});
}

TEST(Emitter, byte)
{
    pure_bin_instr_test(BYTE, &Emitter::byte, 31, 1, 1);
    pure_bin_instr_test(
        BYTE, &Emitter::byte, 0, {0, 0, 0, 0x8877665544332211}, 0x88);
    pure_bin_instr_test(
        BYTE, &Emitter::byte, 4, {0, 0, 0, 0x8877665544332211}, 0x44);
    pure_bin_instr_test(BYTE, &Emitter::byte, 32, {-1, -1, -1, -1}, 0);
}

TEST(Emitter, shl)
{
    pure_bin_instr_test(
        SHL, &Emitter::shl, 255, 1, {0, 0, 0, static_cast<uint64_t>(1) << 63});
    pure_bin_instr_test(
        SHL,
        &Emitter::shl,
        63,
        ~static_cast<uint64_t>(0),
        {static_cast<uint64_t>(1) << 63,
         ~(static_cast<uint64_t>(1) << 63),
         0,
         0});
    pure_bin_instr_test(
        SHL,
        &Emitter::shl,
        127,
        std::numeric_limits<uint256_t>::max(),
        {0,
         static_cast<uint64_t>(1) << 63,
         ~static_cast<uint64_t>(0),
         ~static_cast<uint64_t>(0)});
    pure_bin_instr_test(
        SHL, &Emitter::shl, 256, std::numeric_limits<uint256_t>::max(), 0);
    pure_bin_instr_test(
        SHL, &Emitter::shl, 257, std::numeric_limits<uint256_t>::max(), 0);
}

TEST(Emitter, shl_identity)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SHL});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(2);
    emit.push(0);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.shl();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, shl_0)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SHL});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(2);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.shl();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, shr)
{
    pure_bin_instr_test(SHR, &Emitter::shr, 1, 2, 1);
    pure_bin_instr_test(
        SHR,
        &Emitter::shr,
        63,
        {0, -1, 0, 0},
        {~static_cast<uint64_t>(0) - 1, 1, 0, 0});
    pure_bin_instr_test(
        SHR,
        &Emitter::shr,
        127,
        std::numeric_limits<uint256_t>::max(),
        {~static_cast<uint64_t>(0), ~static_cast<uint64_t>(0), 1, 0});
    pure_bin_instr_test(
        SHR, &Emitter::shr, 256, std::numeric_limits<uint256_t>::max(), 0);
    pure_bin_instr_test(
        SHR, &Emitter::shr, 257, std::numeric_limits<uint256_t>::max(), 0);
}

TEST(Emitter, shr_identity)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SHR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(2);
    emit.push(0);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.shr();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, shr_0)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SHR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(2);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.shr();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, sar)
{
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        63,
        {0, -1, 0, 0},
        {~static_cast<uint64_t>(0) - 1, 1, 0, 0});
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        63,
        {0, -1, 0, -1},
        {~static_cast<uint64_t>(0) - 1,
         1,
         ~static_cast<uint64_t>(0) - 1,
         ~static_cast<uint64_t>(0)});
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        127,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max());
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        256,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max());
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        257,
        std::numeric_limits<uint256_t>::max(),
        std::numeric_limits<uint256_t>::max());
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        256,
        {0, 0, 0, ~(static_cast<uint64_t>(1) << 63)},
        0);
    pure_bin_instr_test(
        SAR,
        &Emitter::sar,
        257,
        {0, 0, 0, ~(static_cast<uint64_t>(1) << 63)},
        0);
}

TEST(Emitter, sar_identity)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SAR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(2);
    emit.push(0);
    mov_literal_to_location_type(emit, 0, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.sar();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, sar_0)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SAR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(2);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.sar();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, sar_max)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, SAR});
    asmjit::JitRuntime const rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(std::numeric_limits<uint256_t>::max());
    emit.push(2);
    mov_literal_to_location_type(emit, 1, Emitter::LocationType::GeneralReg);
    auto e = emit.get_stack().get(0);
    emit.sar();
    ASSERT_EQ(emit.get_stack().get(0), e);
}

TEST(Emitter, call_runtime_pure)
{
    pure_bin_instr_test(
        DIV,
        [](Emitter &emit) {
            emit.call_runtime(0, runtime::udiv<EVMC_FRONTIER>);
        },
        1000,
        4,
        250);
}

TEST(Emitter, call_runtime_impl)
{
    pure_bin_instr_test(
        EXP,
        [](Emitter &emit) {
            emit.call_runtime(0, runtime::exp<EVMC_FRONTIER>);
        },
        10,
        20,
        100000000000000000000_u256);
}

TEST(Emitter, call_runtime_12_arg_fun)
{
    static_assert(Emitter::MAX_RUNTIME_ARGS == 12);
    auto ir = basic_blocks::BasicBlocksIR(
        {PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         CALL,
         RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    for (int32_t i = 0; i < 10; ++i) {
        emit.push(i);
        mov_literal_to_location_type(emit, i, Emitter::LocationType::AvxReg);
    }
    emit.call_runtime(10, runtime_test_12_arg_fun);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(10);
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 5);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 0);
}

TEST(Emitter, call_runtime_11_arg_fun)
{
    static_assert(Emitter::MAX_RUNTIME_ARGS == 12);
    auto ir = basic_blocks::BasicBlocksIR(
        {PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         PUSH0,
         CALL,
         RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    for (int32_t i = 0; i < 9; ++i) {
        emit.push(i);
        mov_literal_to_location_type(emit, i, Emitter::LocationType::AvxReg);
    }
    emit.call_runtime(9, runtime_test_11_arg_fun);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(10);
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 5);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 0);
}

TEST(Emitter, runtime_exit)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, PUSH0, EXP, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.push(300);
    emit.push(10);
    emit.call_runtime(9, runtime::exp<EVMC_SPURIOUS_DRAGON>);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(99);
    auto const &ret = ctx.result;

    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::OutOfGas);
}

TEST(Emitter, address)
{
    auto ir = basic_blocks::BasicBlocksIR({ADDRESS, ADDRESS});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.address();
    emit.address();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    memset(ctx.env.recipient.bytes, 0, sizeof(ctx.env.recipient));
    ctx.env.recipient.bytes[0] = 2;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 2);
}

TEST(Emitter, caller)
{
    auto ir = basic_blocks::BasicBlocksIR({CALLER, CALLER});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.caller();
    emit.caller();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    memset(ctx.env.sender.bytes, 0, sizeof(ctx.env.sender));
    ctx.env.sender.bytes[0] = 1;
    ctx.env.sender.bytes[1] = 1;
    ctx.env.sender.bytes[2] = 1;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0x010101);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 0x010101);
}

TEST(Emitter, calldatasize)
{
    auto ir = basic_blocks::BasicBlocksIR({CALLDATASIZE, CALLDATASIZE, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.calldatasize();
    emit.calldatasize();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    ctx.env.input_data_size = 5;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 5);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 5);
}

TEST(Emitter, returndatasize)
{
    auto ir =
        basic_blocks::BasicBlocksIR({RETURNDATASIZE, RETURNDATASIZE, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.returndatasize();
    emit.returndatasize();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    ctx.env.return_data_size = 6;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 6);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 6);
}

TEST(Emitter, msize)
{
    auto ir = basic_blocks::BasicBlocksIR({MSIZE, MSIZE, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.msize();
    emit.msize();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    ctx.memory.size = 7;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 7);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 7);
}

TEST(Emitter, gas)
{
    auto ir = basic_blocks::BasicBlocksIR({GAS, GAS, RETURN});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.gas(2);
    emit.gas(2);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context(10);
    auto const &ret = ctx.result;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 12);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 12);
}

TEST(Emitter, callvalue)
{
    auto ir = basic_blocks::BasicBlocksIR({CALLVALUE, CALLVALUE});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.callvalue();
    emit.callvalue();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    memset(ctx.env.value.bytes, 0, sizeof(ctx.env.value));
    ctx.env.value.bytes[1] = 3;

    entry(&ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0x0300);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 0x0300);
}

TEST(Emitter, iszero)
{
    pure_una_instr_test(ISZERO, &Emitter::iszero, 0, 1);
    pure_una_instr_test(ISZERO, &Emitter::iszero, 1, 0);
    pure_una_instr_test(ISZERO, &Emitter::iszero, -1, 0);
    pure_una_instr_test(
        ISZERO, &Emitter::iszero, std::numeric_limits<uint256_t>::max(), 0);
}

TEST(Emitter, not_)
{
    pure_una_instr_test(
        NOT, &Emitter::not_, 0, std::numeric_limits<uint256_t>::max());
    pure_una_instr_test(
        NOT, &Emitter::not_, 1, std::numeric_limits<uint256_t>::max() - 1);
    pure_una_instr_test(NOT, &Emitter::not_, -1, {0, -1, -1, -1});
    pure_una_instr_test(
        NOT, &Emitter::not_, std::numeric_limits<uint256_t>::max(), 0);
}

TEST(Emitter, jump)
{
    std::vector<Emitter::LocationType> const locs = {
        Emitter::LocationType::Literal,
        Emitter::LocationType::AvxReg,
        Emitter::LocationType::GeneralReg,
        Emitter::LocationType::StackOffset};
    for (auto loc1 : locs) {
        for (auto loc2 : locs) {
            for (auto loc_dest : locs) {
                jump_test(loc1, loc2, loc_dest, false);
                jump_test(loc1, loc2, loc_dest, true);
            }
        }
    }
}

TEST(Emitter, jump_invalid)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, JUMP});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(0);
    emit.jump();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::InvalidInstruction);
}

TEST(Emitter, jumpi)
{
    std::vector<Emitter::LocationType> const locs = {
        Emitter::LocationType::Literal,
        Emitter::LocationType::AvxReg,
        Emitter::LocationType::GeneralReg,
        Emitter::LocationType::StackOffset};
    for (auto loc1 : locs) {
        for (auto loc2 : locs) {
            for (auto loc_cond : locs) {
                for (auto loc_dest : locs) {
                    for (int8_t i = 0; i < 16; ++i) {
                        jumpi_test(
                            loc1,
                            loc2,
                            loc_cond,
                            loc_dest,
                            i & 1,
                            i & 2,
                            i & 4,
                            i & 8);
                    }
                }
            }
        }
    }
}

TEST(Emitter, jumpi_invalid)
{
    auto ir = basic_blocks::BasicBlocksIR({PUSH0, PUSH0, JUMPI});

    asmjit::JitRuntime rt;
    Emitter emit{rt, ir.codesize};
    (void)emit.begin_new_block(ir.blocks()[0]);
    emit.push(1);
    emit.push(1);
    emit.jumpi();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ctx = test_context();
    auto const &ret = ctx.result;
    auto stack_memory = test_stack_memory();
    entry(&ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::InvalidInstruction);
}

TEST(Emitter, block_epilogue)
{
    std::vector<Emitter::LocationType> const locs = {
        Emitter::LocationType::Literal,
        Emitter::LocationType::AvxReg,
        Emitter::LocationType::GeneralReg,
        Emitter::LocationType::StackOffset};
    for (auto loc1 : locs) {
        if (loc1 == Emitter::LocationType::Literal) {
            continue;
        }
        for (auto loc2 : locs) {
            if (loc2 == Emitter::LocationType::Literal) {
                continue;
            }
            for (auto loc3 : locs) {
                for (auto loc4 : locs) {
                    for (auto loc5 : locs) {
                        block_epilogue_test(loc1, loc2, loc3, loc4, loc5);
                    }
                }
            }
        }
    }
}
