#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>
#include <compiler/opcodes.h>
#include <utils/assert.h>

#include <evmc/evmc.h>

#include <asmjit/core/jitruntime.h>

#include <cassert>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <utility>

using namespace monad::compiler;
using namespace monad::compiler::local_stacks;
using namespace monad::compiler::native;

namespace
{
    struct BlockAnalysis
    {
        int64_t instr_gas;
        bool dynamic_gas;
    };

    BlockAnalysis analyze_block(Block const &block)
    {
        BlockAnalysis analysis{.instr_gas = 0, .dynamic_gas = false};
        for (auto const &instr : block.instrs) {
            analysis.instr_gas += instr.static_gas_cost();
            analysis.dynamic_gas |= instr.dynamic_gas();
        }
        auto const &info = basic_blocks::terminator_info(block.terminator);
        // This is also correct for fall through and invalid instruction.
        analysis.instr_gas += info.min_gas;
        analysis.dynamic_gas |= info.dynamic_gas;
        return analysis;
    }

    template <evmc_revision rev>
    void emit_instr(
        Emitter &emit, LocalStacksIR const &ir, Instruction const &instr,
        uint32_t remaining_base_gas)
    {
        (void)ir;
        (void)remaining_base_gas;

        if (instr.is_push()) {
            emit.push(instr.immediate_value());
        }

        if (instr.is_dup()) {
            std::terminate();
        }

        if (instr.is_swap()) {
            std::terminate();
        }

        if (instr.is_log()) {
            std::terminate();
        }

        switch (instr.opcode()) {
        case ADD:
            std::terminate();
            break;
        case MUL:
            std::terminate();
            break;
        case SUB:
            std::terminate();
            break;
        case DIV:
            std::terminate();
            break;
        case SDIV:
            std::terminate();
            break;
        case MOD:
            std::terminate();
            break;
        case SMOD:
            std::terminate();
            break;
        case ADDMOD:
            std::terminate();
            break;
        case MULMOD:
            std::terminate();
            break;
        case EXP:
            std::terminate();
            break;
        case SIGNEXTEND:
            std::terminate();
            break;
        case LT:
            std::terminate();
            break;
        case GT:
            std::terminate();
            break;
        case SLT:
            std::terminate();
            break;
        case SGT:
            std::terminate();
            break;
        case EQ:
            std::terminate();
            break;
        case ISZERO:
            std::terminate();
            break;
        case AND:
            std::terminate();
            break;
        case OR:
            std::terminate();
            break;
        case XOR:
            std::terminate();
            break;
        case NOT:
            std::terminate();
            break;
        case BYTE:
            std::terminate();
            break;
        case SHL:
            std::terminate();
            break;
        case SHR:
            std::terminate();
            break;
        case SAR:
            std::terminate();
            break;
        case SHA3:
            std::terminate();
            break;
        case ADDRESS:
            std::terminate();
            break;
        case BALANCE:
            std::terminate();
            break;
        case ORIGIN:
            std::terminate();
            break;
        case CALLER:
            std::terminate();
            break;
        case CALLVALUE:
            std::terminate();
            break;
        case CALLDATALOAD:
            std::terminate();
            break;
        case CALLDATASIZE:
            std::terminate();
            break;
        case CALLDATACOPY:
            std::terminate();
            break;
        case CODESIZE:
            std::terminate();
            break;
        case CODECOPY:
            std::terminate();
            break;
        case GASPRICE:
            std::terminate();
            break;
        case EXTCODESIZE:
            std::terminate();
            break;
        case EXTCODECOPY:
            std::terminate();
            break;
        case RETURNDATASIZE:
            std::terminate();
            break;
        case RETURNDATACOPY:
            std::terminate();
            break;
        case EXTCODEHASH:
            std::terminate();
            break;
        case BLOCKHASH:
            std::terminate();
            break;
        case COINBASE:
            std::terminate();
            break;
        case TIMESTAMP:
            std::terminate();
            break;
        case NUMBER:
            std::terminate();
            break;
        case DIFFICULTY:
            std::terminate();
            break;
        case GASLIMIT:
            std::terminate();
            break;
        case CHAINID:
            std::terminate();
            break;
        case SELFBALANCE:
            std::terminate();
            break;
        case BASEFEE:
            std::terminate();
            break;
        case BLOBHASH:
            std::terminate();
            break;
        case BLOBBASEFEE:
            std::terminate();
            break;
        case POP:
            std::terminate();
            break;
        case MLOAD:
            std::terminate();
            break;
        case MSTORE:
            std::terminate();
            break;
        case MSTORE8:
            std::terminate();
            break;
        case SLOAD:
            std::terminate();
            break;
        case SSTORE:
            std::terminate();
            break;
        case PC:
            std::terminate();
            break;
        case MSIZE:
            std::terminate();
            break;
        case GAS:
            std::terminate();
            break;
        case TLOAD:
            std::terminate();
            break;
        case TSTORE:
            std::terminate();
            break;
        case MCOPY:
            std::terminate();
            break;
        case CREATE:
            std::terminate();
            break;
        case CALL:
            std::terminate();
            break;
        case CALLCODE:
            std::terminate();
            break;
        case DELEGATECALL:
            std::terminate();
            break;
        case CREATE2:
            std::terminate();
            break;
        case STATICCALL:
            std::terminate();
            break;
        default:
            std::unreachable();
        }
    }

    template <evmc_revision rev>
    void emit_instrs(
        Emitter &emit, LocalStacksIR const &ir, Block const &block,
        int64_t instr_gas)
    {
        MONAD_COMPILER_ASSERT(
            instr_gas <= std::numeric_limits<uint32_t>::max());
        uint32_t remaining_base_gas = static_cast<uint32_t>(instr_gas);
        for (auto const &instr : block.instrs) {
            assert(remaining_base_gas >= instr.static_gas_cost());
            remaining_base_gas -=
                static_cast<uint32_t>(instr.static_gas_cost());
            emit_instr<rev>(emit, ir, instr, remaining_base_gas);
        }
    }

    template <evmc_revision rev>
    void
    emit_terminator(Emitter &emit, LocalStacksIR const &ir, Block const &block)
    {
        (void)emit;
        (void)ir;
        using enum basic_blocks::Terminator;
        switch (block.terminator) {
        case FallThrough:
            std::terminate();
            break;
        case JumpI:
            std::terminate();
            break;
        case Jump:
            std::terminate();
            break;
        case Return:
            std::terminate();
            break;
        case Stop:
            std::terminate();
            break;
        case Revert:
            std::terminate();
            break;
        case SelfDestruct:
            std::terminate();
            break;
        case InvalidInstruction:
            std::terminate();
            break;
        default:
            std::unreachable();
        }
    }

    void emit_gas_check(
        Emitter &emit, LocalStacksIR const &ir, Block const &block,
        BlockAnalysis const &analysis)
    {
        // Arbitrary gas threshold for when to emit gas check.
        // Needs to be big enough to make the gas check insignificant,
        // and small enough to avoid exploitation of the optimization.
        static int64_t const STATIC_GAS_CHECK_THRESHOLD = 1000;

        int64_t const gas = ir.jumpdests.contains(block.offset)
                                ? analysis.instr_gas + 1
                                : analysis.instr_gas;
        if (!analysis.dynamic_gas ||
            analysis.instr_gas >= STATIC_GAS_CHECK_THRESHOLD) {
            emit.gas_decrement_no_check(gas);
        }
        else {
            emit.gas_decrement_check_non_negative(gas);
        }
    }

    template <evmc_revision rev>
    entrypoint_t
    compile_local_stacks(asmjit::JitRuntime &rt, LocalStacksIR const &ir)
    {
        Emitter emit{rt};
        for (Block const &block : ir.blocks) {
            emit.begin_stack(block);
            bool const can_enter_block = emit.block_prologue(block);
            if (can_enter_block) {
                auto analysis = analyze_block(block);
                emit_gas_check(emit, ir, block, analysis);
                emit_instrs<rev>(emit, ir, block, analysis.instr_gas);
                emit_terminator<rev>(emit, ir, block);
            }
        }
        return emit.finish_contract(rt);
    }

    template <evmc_revision rev>
    entrypoint_t
    compile_contract(asmjit::JitRuntime &rt, std::span<uint8_t const> contract)
    {
        // TODO - Need to change opcode table to depend on revision.
        auto ir = LocalStacksIR(basic_blocks::BasicBlocksIR(contract));
        return compile_local_stacks<rev>(rt, ir);
    }
}

namespace monad::compiler::native
{
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev)
    {
        try {
            (void)rev;
            // TODO - branch on revision here?
            return ::compile_contract<EVMC_CANCUN>(rt, contract);
        }
        catch (Emitter::Error const &e) {
            (void)e;
            return std::nullopt;
        }
    }
}
