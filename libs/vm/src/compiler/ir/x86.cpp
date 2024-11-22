#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
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
        using enum OpCode;
        (void)ir;
        (void)remaining_base_gas;

        switch (instr.opcode()) {
        case Add:
            std::terminate();
            break;
        case Mul:
            std::terminate();
            break;
        case Sub:
            std::terminate();
            break;
        case Div:
            std::terminate();
            break;
        case SDiv:
            std::terminate();
            break;
        case Mod:
            std::terminate();
            break;
        case SMod:
            std::terminate();
            break;
        case AddMod:
            std::terminate();
            break;
        case MulMod:
            std::terminate();
            break;
        case Exp:
            std::terminate();
            break;
        case SignExtend:
            std::terminate();
            break;
        case Lt:
            std::terminate();
            break;
        case Gt:
            std::terminate();
            break;
        case SLt:
            std::terminate();
            break;
        case SGt:
            std::terminate();
            break;
        case Eq:
            std::terminate();
            break;
        case IsZero:
            std::terminate();
            break;
        case And:
            std::terminate();
            break;
        case Or:
            std::terminate();
            break;
        case XOr:
            std::terminate();
            break;
        case Not:
            std::terminate();
            break;
        case Byte:
            std::terminate();
            break;
        case Shl:
            std::terminate();
            break;
        case Shr:
            std::terminate();
            break;
        case Sar:
            std::terminate();
            break;
        case Sha3:
            std::terminate();
            break;
        case Address:
            std::terminate();
            break;
        case Balance:
            std::terminate();
            break;
        case Origin:
            std::terminate();
            break;
        case Caller:
            std::terminate();
            break;
        case CallValue:
            std::terminate();
            break;
        case CallDataLoad:
            std::terminate();
            break;
        case CallDataSize:
            std::terminate();
            break;
        case CallDataCopy:
            std::terminate();
            break;
        case CodeSize:
            std::terminate();
            break;
        case CodeCopy:
            std::terminate();
            break;
        case GasPrice:
            std::terminate();
            break;
        case ExtCodeSize:
            std::terminate();
            break;
        case ExtCodeCopy:
            std::terminate();
            break;
        case ReturnDataSize:
            std::terminate();
            break;
        case ReturnDataCopy:
            std::terminate();
            break;
        case ExtCodeHash:
            std::terminate();
            break;
        case BlockHash:
            std::terminate();
            break;
        case Coinbase:
            std::terminate();
            break;
        case Timestamp:
            std::terminate();
            break;
        case Number:
            std::terminate();
            break;
        case Difficulty:
            std::terminate();
            break;
        case GasLimit:
            std::terminate();
            break;
        case ChainId:
            std::terminate();
            break;
        case SelfBalance:
            std::terminate();
            break;
        case BaseFee:
            std::terminate();
            break;
        case BlobHash:
            std::terminate();
            break;
        case BlobBaseFee:
            std::terminate();
            break;
        case Pop:
            std::terminate();
            break;
        case Push:
            emit.push(instr.immediate_value());
            break;
        case Dup:
            std::terminate();
            break;
        case Swap:
            std::terminate();
            break;
        case MLoad:
            std::terminate();
            break;
        case MStore:
            std::terminate();
            break;
        case MStore8:
            std::terminate();
            break;
        case SLoad:
            std::terminate();
            break;
        case SStore:
            std::terminate();
            break;
        case Pc:
            std::terminate();
            break;
        case MSize:
            std::terminate();
            break;
        case Gas:
            std::terminate();
            break;
        case Log:
            std::terminate();
            break;
        case TLoad:
            std::terminate();
            break;
        case TStore:
            std::terminate();
            break;
        case MCopy:
            std::terminate();
            break;
        case Create:
            std::terminate();
            break;
        case Call:
            std::terminate();
            break;
        case CallCode:
            std::terminate();
            break;
        case DelegateCall:
            std::terminate();
            break;
        case Create2:
            std::terminate();
            break;
        case StaticCall:
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
