#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>
#include <utils/assert.h>

#include <evmc/evmc.h>

#include <asmjit/core/jitruntime.h>

#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <utility>

using namespace monad::compiler;
using namespace monad::compiler::basic_blocks;
using namespace monad::compiler::native;

namespace
{
    struct BlockAnalysis
    {
        int32_t instr_gas;
        bool dynamic_gas;
    };

    BlockAnalysis analyze_block(Block const &block)
    {
        BlockAnalysis analysis{.instr_gas = 0, .dynamic_gas = false};
        for (auto const &instr : block.instrs) {
            analysis.instr_gas += instr.static_gas_cost();
            analysis.dynamic_gas |= instr.dynamic_gas();
        }
        auto [static_gas, dynamic_gas] =
            basic_blocks::terminator_gas_info(block.terminator);
        // This is also correct for fall through and invalid instruction.
        analysis.instr_gas += static_gas;
        analysis.dynamic_gas |= dynamic_gas;
        return analysis;
    }

    template <evmc_revision rev>
    void emit_instr(
        Emitter &emit, BasicBlocksIR const &ir, Instruction const &instr,
        int32_t remaining_base_gas)
    {
        using enum OpCode;
        (void)ir;
        (void)remaining_base_gas;

        switch (instr.opcode()) {
        case Add:
            emit.add();
            break;
        case Mul:
            std::terminate();
            break;
        case Sub:
            emit.sub();
            break;
        case Div:
            emit.call_runtime(remaining_base_gas, monad::runtime::udiv<rev>);
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
            emit.lt();
            break;
        case Gt:
            emit.gt();
            break;
        case SLt:
            emit.slt();
            break;
        case SGt:
            emit.sgt();
            break;
        case Eq:
            emit.eq();
            break;
        case IsZero:
            emit.iszero();
            break;
        case And:
            emit.and_();
            break;
        case Or:
            emit.or_();
            break;
        case XOr:
            emit.xor_();
            break;
        case Not:
            emit.not_();
            break;
        case Byte:
            emit.byte();
            break;
        case Shl:
            emit.shl();
            break;
        case Shr:
            emit.shr();
            break;
        case Sar:
            emit.sar();
            break;
        case Sha3:
            std::terminate();
            break;
        case Address:
            emit.address();
            break;
        case Balance:
            std::terminate();
            break;
        case Origin:
            std::terminate();
            break;
        case Caller:
            emit.caller();
            break;
        case CallValue:
            emit.callvalue();
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
            emit.pop();
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
            emit.push(instr.pc());
            break;
        case MSize:
            std::terminate();
            break;
        case Gas:
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
        case Push:
            emit.push(instr.immediate_value());
            break;
        case Dup:
            emit.dup(instr.index());
            break;
        case Swap:
            emit.swap(instr.index());
            break;
        case Log:
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
        Emitter &emit, BasicBlocksIR const &ir, Block const &block,
        int32_t instr_gas)
    {
        MONAD_COMPILER_ASSERT(instr_gas <= std::numeric_limits<int32_t>::max());
        int32_t remaining_base_gas = instr_gas;
        for (auto const &instr : block.instrs) {
            MONAD_COMPILER_DEBUG_ASSERT(
                remaining_base_gas >= instr.static_gas_cost());
            remaining_base_gas -= instr.static_gas_cost();
            emit_instr<rev>(emit, ir, instr, remaining_base_gas);
        }
    }

    template <evmc_revision rev>
    void emit_terminator(Emitter &emit, Block const &block)
    {
        using enum basic_blocks::Terminator;
        switch (block.terminator) {
        case FallThrough:
            emit.fallthrough();
            break;
        case JumpI:
            emit.jumpi();
            break;
        case Jump:
            emit.jump();
            break;
        case Return:
            emit.return_();
            break;
        case Stop:
            emit.stop();
            break;
        case Revert:
            emit.revert();
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
        Emitter &emit, BasicBlocksIR const &ir, Block const &block,
        BlockAnalysis const &analysis)
    {
        // Arbitrary gas threshold for when to emit gas check.
        // Needs to be big enough to make the gas check insignificant,
        // and small enough to avoid exploitation of the optimization.
        static int32_t const STATIC_GAS_CHECK_THRESHOLD = 1000;

        int32_t const gas = ir.jump_dests().contains(block.offset)
                                ? analysis.instr_gas + 1
                                : analysis.instr_gas;
        if (!analysis.dynamic_gas || gas >= STATIC_GAS_CHECK_THRESHOLD) {
            emit.gas_decrement_no_check(gas);
        }
        else {
            emit.gas_decrement_check_non_negative(gas);
        }
    }

    template <evmc_revision rev>
    entrypoint_t
    compile_local_stacks(asmjit::JitRuntime &rt, BasicBlocksIR const &ir)
    {
        Emitter emit{rt, ir.codesize};
        for (auto const &[d, _] : ir.jump_dests()) {
            emit.add_jump_dest(d);
        }
        for (Block const &block : ir.blocks()) {
            bool const can_enter_block = emit.begin_new_block(block);
            if (can_enter_block) {
                auto analysis = analyze_block(block);
                emit_gas_check(emit, ir, block, analysis);
                emit_instrs<rev>(emit, ir, block, analysis.instr_gas);
                emit_terminator<rev>(emit, block);
            }
        }
        return emit.finish_contract(rt);
    }

    template <evmc_revision Rev>
    entrypoint_t
    compile_contract(asmjit::JitRuntime &rt, std::span<uint8_t const> contract)
    {
        auto ir = BasicBlocksIR(basic_blocks::make_ir<Rev>(contract));
        return compile_local_stacks<Rev>(rt, ir);
    }
}

namespace monad::compiler::native
{
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev)
    {
        try {
            switch (rev) {
            case EVMC_FRONTIER:
                return ::compile_contract<EVMC_FRONTIER>(rt, contract);
            case EVMC_HOMESTEAD:
                return ::compile_contract<EVMC_HOMESTEAD>(rt, contract);
            case EVMC_TANGERINE_WHISTLE:
                return ::compile_contract<EVMC_TANGERINE_WHISTLE>(rt, contract);
            case EVMC_SPURIOUS_DRAGON:
                return ::compile_contract<EVMC_SPURIOUS_DRAGON>(rt, contract);
            case EVMC_BYZANTIUM:
                return ::compile_contract<EVMC_BYZANTIUM>(rt, contract);
            case EVMC_CONSTANTINOPLE:
                return ::compile_contract<EVMC_CONSTANTINOPLE>(rt, contract);
            case EVMC_PETERSBURG:
                return ::compile_contract<EVMC_PETERSBURG>(rt, contract);
            case EVMC_ISTANBUL:
                return ::compile_contract<EVMC_ISTANBUL>(rt, contract);
            case EVMC_BERLIN:
                return ::compile_contract<EVMC_BERLIN>(rt, contract);
            case EVMC_LONDON:
                return ::compile_contract<EVMC_LONDON>(rt, contract);
            case EVMC_PARIS:
                return ::compile_contract<EVMC_PARIS>(rt, contract);
            case EVMC_SHANGHAI:
                return ::compile_contract<EVMC_SHANGHAI>(rt, contract);
            case EVMC_CANCUN:
                return ::compile_contract<EVMC_CANCUN>(rt, contract);
            default:
                return std::nullopt;
            }
        }
        catch (Emitter::Error const &e) {
            (void)e;
            return std::nullopt;
        }
    }
}
