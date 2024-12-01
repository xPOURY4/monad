#include "asmjit/core/jitruntime.h"
#include "compiler/evm_opcodes.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/x86/virtual_stack.h"
#include "compiler/types.h"
#include "evmc/evmc.hpp"
#include "intx/intx.hpp"
#include "runtime/types.h"
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace runtime = monad::runtime;
using namespace monad::compiler;
using namespace monad::compiler::native;

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

    runtime::Context test_context(int64_t gas_remaining = 10)
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
                },
            .memory = {},
            .memory_cost = 0,
        };
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
            emit.mov_stack_index_to_general_reg_update_eflags(stack_index);
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

    using PureEmitterInstr = void (Emitter::*)();

    void pure_bin_instr_test_instance(
        PureEmitterInstr instr, uint256_t const &left,
        Emitter::LocationType left_loc, uint256_t const &right,
        Emitter::LocationType right_loc, uint256_t const &result,
        local_stacks::LocalStacksIR const &ir, bool dup)
    {
#if 0
        if (left_loc != Emitter::LocationType::GeneralReg || right_loc != Emitter::LocationType::GeneralReg || !dup) {
            return;
        }
#endif
        asmjit::JitRuntime rt;

        Emitter emit{rt};
        emit.begin_stack(ir.blocks[0]);
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

        (emit.*instr)();

        if (dup) {
            emit.swap(2);
            emit.swap(1);
            (emit.*instr)();
        }
        else {
            emit.push(0);
        }
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ret = test_result();
        auto ctx = test_context();

        auto stack_memory = test_stack_memory();
        entry(&ret, &ctx, stack_memory.get());

#if 0
        std::cout <<
            std::format("LEFT {} : {}  and  RIGHT {} : {}",
                    left, Emitter::location_type_to_string(left_loc),
                    right, Emitter::location_type_to_string(right_loc)) << std::endl;
#endif

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
        local_stacks::LocalStacksIR const &ir, bool dup)
    {
#if 0
        if (loc != Emitter::LocationType::GeneralReg || dup) {
            return;
        }
#endif
        asmjit::JitRuntime rt;

        Emitter emit{rt};
        emit.begin_stack(ir.blocks[0]);
        emit.push(input);
        if (dup) {
            emit.dup(1);
        }

        mov_literal_to_location_type(emit, dup, loc);

        (emit.*instr)();

        if (dup) {
            emit.swap(1);
            (emit.*instr)();
        }
        else {
            emit.push(0);
        }
        emit.return_();

        entrypoint_t entry = emit.finish_contract(rt);
        auto ret = test_result();
        auto ctx = test_context();

        auto stack_memory = test_stack_memory();
        entry(&ret, &ctx, stack_memory.get());

#if 0
        std::cout <<
            std::format("INPUT {} : {} with dup = {}",
                    input, Emitter::location_type_to_string(loc), dup)
            << std::endl;
#endif

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
        auto ir1 = local_stacks::LocalStacksIR(
            basic_blocks::BasicBlocksIR(std::move(bytecode1)));
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
            DUP1,
            PUSH0,
            DUP1,
            SWAP2,
            SWAP1,
            opcode,
            POP,
            opcode,
            RETURN};
        auto ir2 = local_stacks::LocalStacksIR(
            basic_blocks::BasicBlocksIR(std::move(bytecode2)));
        for (auto left_loc : locs) {
            for (auto right_loc : locs) {
                pure_bin_instr_test_instance(
                    instr, left, left_loc, right, right_loc, result, ir2, true);
            }
        }
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
        auto ir1 = local_stacks::LocalStacksIR(
            basic_blocks::BasicBlocksIR(std::move(bytecode1)));
        for (auto loc : locs) {
            pure_una_instr_test_instance(instr, input, loc, result, ir1, false);
        }

        std::vector<uint8_t> bytecode2{
            PUSH0, DUP1, opcode, SWAP1, opcode, RETURN};
        auto ir2 = local_stacks::LocalStacksIR(
            basic_blocks::BasicBlocksIR(std::move(bytecode2)));
        for (auto loc : locs) {
            pure_una_instr_test_instance(instr, input, loc, result, ir1, true);
        }
    }
}

TEST(Emitter, empty)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(
        static_cast<uint64_t>(ret.status),
        std::numeric_limits<uint64_t>::max());
}

TEST(Emitter, stop)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, gas_decrement_no_check_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_no_check(2);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 3);
}

TEST(Emitter, gas_decrement_no_check_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_no_check(7);

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -2);
}

TEST(Emitter, gas_decrement_check_non_negative_1)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(6);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, -1);
    ASSERT_EQ(ret.status, runtime::StatusCode::OutOfGas);
}

TEST(Emitter, gas_decrement_check_non_negative_2)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(5);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 0);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, gas_decrement_check_non_negative_3)
{
    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.gas_decrement_check_non_negative(4);
    emit.stop();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context(5);

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ctx.gas_remaining, 1);
    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
}

TEST(Emitter, return_)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    uint256_t const size_value = uint256_t{1} << 255;
    uint256_t const offset_value =
        std::numeric_limits<uint256_t>::max() - (uint256_t{1} << 31) + 1;
    emit.push(size_value);
    emit.push(offset_value);
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), offset_value);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), size_value);
}

TEST(Emitter, revert)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    uint256_t const size_value = uint256_t{1} << 31;
    uint256_t const offset_value = (uint256_t{1} << 31) - 1;
    emit.push(size_value);
    emit.push(offset_value);
    emit.revert();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(ret.status, runtime::StatusCode::Revert);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), offset_value);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), size_value);
}

TEST(Emitter, mov_stack_index_to_avx_reg)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
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

    emit.mov_stack_index_to_general_reg_update_eflags(0);
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
    auto ret = test_result();
    auto ctx = test_context();

    auto stack_memory = test_stack_memory();
    entry(&ret, &ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, mov_stack_index_to_general_reg_update_eflags)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    Stack &stack = emit.get_stack();
    emit.push(1);
    emit.push(2);

    auto e1 = stack.get(1);

    emit.mov_stack_index_to_general_reg_update_eflags(
        1); // literal -> general reg
    stack.spill_literal(e1);
    ASSERT_TRUE(
        e1->general_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_general_reg_update_eflags(
        1); // general reg -> general reg
    ASSERT_TRUE(
        e1->general_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->avx_reg());

    emit.mov_stack_index_to_avx_reg(1);
    stack.spill_stack_offset(e1);
    (void)stack.spill_general_reg(e1);
    ASSERT_TRUE(
        e1->avx_reg() && !e1->stack_offset() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_general_reg_update_eflags(
        1); // avx reg -> stack offset & general reg
    (void)stack.spill_avx_reg(e1);
    (void)stack.spill_general_reg(e1);
    ASSERT_TRUE(
        e1->stack_offset() && !e1->avx_reg() && !e1->literal() &&
        !e1->general_reg());

    emit.mov_stack_index_to_general_reg_update_eflags(
        1); // stack offset -> general reg
    stack.spill_stack_offset(e1);
    ASSERT_TRUE(
        e1->general_reg() && !e1->avx_reg() && !e1->literal() &&
        !e1->stack_offset());

    e1.reset();

    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();

    auto stack_memory = test_stack_memory();
    entry(&ret, &ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, mov_stack_index_to_stack_offset)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({PUSH1, 1, PUSH1, 2}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
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

    emit.mov_stack_index_to_general_reg_update_eflags(
        1); // stack offset -> general reg
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
    auto ret = test_result();
    auto ctx = test_context();

    auto stack_memory = test_stack_memory();
    entry(&ret, &ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, discharge_deferred_comparison)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
        {PUSH0, PUSH0, LT, DUP1, DUP1, PUSH0, SWAP1, POP, LT, RETURN}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
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
    auto ret = test_result();
    auto ctx = test_context();

    auto stack_memory = test_stack_memory();
    entry(&ret, &ctx, stack_memory.get());

    ASSERT_EQ(ret.status, runtime::StatusCode::Success);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 1);
}

TEST(Emitter, discharge_negated_deferred_comparison)
{
    auto ir = local_stacks::LocalStacksIR(basic_blocks::BasicBlocksIR(
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
         RETURN}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
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
    auto ret = test_result();
    auto ctx = test_context();

    auto stack_memory = test_stack_memory();
    entry(&ret, &ctx, stack_memory.get());

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

TEST(Emitter, address)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({ADDRESS, ADDRESS}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    emit.address();
    emit.address();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();
    memset(ctx.env.recipient.bytes, 0, sizeof(ctx.env.recipient));
    ctx.env.recipient.bytes[0] = 2;

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 2);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 2);
}

TEST(Emitter, caller)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({CALLER, CALLER}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    emit.caller();
    emit.caller();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();
    memset(ctx.env.sender.bytes, 0, sizeof(ctx.env.sender));
    ctx.env.sender.bytes[0] = 1;
    ctx.env.sender.bytes[1] = 1;
    ctx.env.sender.bytes[2] = 1;

    entry(&ret, &ctx, nullptr);

    ASSERT_EQ(intx::le::load<uint256_t>(ret.offset), 0x010101);
    ASSERT_EQ(intx::le::load<uint256_t>(ret.size), 0x010101);
}

TEST(Emitter, callvalue)
{
    auto ir = local_stacks::LocalStacksIR(
        basic_blocks::BasicBlocksIR({CALLVALUE, CALLVALUE}));

    asmjit::JitRuntime rt;
    Emitter emit{rt};
    emit.begin_stack(ir.blocks[0]);
    emit.callvalue();
    emit.callvalue();
    emit.return_();

    entrypoint_t entry = emit.finish_contract(rt);
    auto ret = test_result();
    auto ctx = test_context();
    memset(ctx.env.value.bytes, 0, sizeof(ctx.env.value));
    ctx.env.value.bytes[1] = 3;

    entry(&ret, &ctx, nullptr);

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
