#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/x86.h>
#include <compiler/ir/x86/emitter.h>
#include <utils/assert.h>

#include <evmc/evmc.h>

#include <asmjit/core/jitruntime.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>

using namespace monad::compiler;
using namespace monad::compiler::basic_blocks;
using namespace monad::compiler::native;

namespace
{
    template <evmc_revision Rev>
    int32_t block_base_gas(Block const &block)
    {
        int32_t base_gas = 0;
        for (auto const &instr : block.instrs) {
            base_gas += instr.static_gas_cost();
        }
        auto term_gas =
            basic_blocks::terminator_static_gas<Rev>(block.terminator);
        // This is also correct for fall through and invalid instruction:
        return base_gas + term_gas;
    }

    template <evmc_revision rev>
    void emit_instr(
        Emitter &emit, Instruction const &instr, int32_t remaining_base_gas)
    {
        using enum OpCode;
        switch (instr.opcode()) {
        case Add:
            emit.add();
            break;
        case Mul:
            emit.call_runtime(remaining_base_gas, monad::runtime::mul<rev>);
            break;
        case Sub:
            emit.sub();
            break;
        case Div:
            emit.call_runtime(remaining_base_gas, monad::runtime::udiv<rev>);
            break;
        case SDiv:
            emit.call_runtime(remaining_base_gas, monad::runtime::sdiv<rev>);
            break;
        case Mod:
            emit.call_runtime(remaining_base_gas, monad::runtime::umod<rev>);
            break;
        case SMod:
            emit.call_runtime(remaining_base_gas, monad::runtime::smod<rev>);
            break;
        case AddMod:
            emit.call_runtime(remaining_base_gas, monad::runtime::addmod<rev>);
            break;
        case MulMod:
            emit.call_runtime(remaining_base_gas, monad::runtime::mulmod<rev>);
            break;
        case Exp:
            emit.call_runtime(remaining_base_gas, monad::runtime::exp<rev>);
            break;
        case SignExtend:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::signextend<rev>);
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
            emit.call_runtime(remaining_base_gas, monad::runtime::sha3<rev>);
            break;
        case Address:
            emit.address();
            break;
        case Balance:
            emit.call_runtime(remaining_base_gas, monad::runtime::balance<rev>);
            break;
        case Origin:
            emit.call_runtime(remaining_base_gas, monad::runtime::origin<rev>);
            break;
        case Caller:
            emit.caller();
            break;
        case CallValue:
            emit.callvalue();
            break;
        case CallDataLoad:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::calldataload<rev>);
            break;
        case CallDataSize:
            emit.calldatasize();
            break;
        case CallDataCopy:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::calldatacopy<rev>);
            break;
        case CodeSize:
            emit.codesize();
            break;
        case CodeCopy:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::codecopy<rev>);
            break;
        case GasPrice:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::gasprice<rev>);
            break;
        case ExtCodeSize:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::extcodesize<rev>);
            break;
        case ExtCodeCopy:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::extcodecopy<rev>);
            break;
        case ReturnDataSize:
            emit.returndatasize();
            break;
        case ReturnDataCopy:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::returndatacopy<rev>);
            break;
        case ExtCodeHash:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::extcodehash<rev>);
            break;
        case BlockHash:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::blockhash<rev>);
            break;
        case Coinbase:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::coinbase<rev>);
            break;
        case Timestamp:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::timestamp<rev>);
            break;
        case Number:
            emit.call_runtime(remaining_base_gas, monad::runtime::number<rev>);
            break;
        case Difficulty:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::prevrandao<rev>);
            break;
        case GasLimit:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::gaslimit<rev>);
            break;
        case ChainId:
            emit.call_runtime(remaining_base_gas, monad::runtime::chainid<rev>);
            break;
        case SelfBalance:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::selfbalance<rev>);
            break;
        case BaseFee:
            emit.call_runtime(remaining_base_gas, monad::runtime::basefee<rev>);
            break;
        case BlobHash:
            // TODO - missing implementation
            break;
        case BlobBaseFee:
            emit.push(0);
            // TODO - missing implementation
            break;
        case Pop:
            emit.pop();
            break;
        case MLoad:
            emit.call_runtime(remaining_base_gas, monad::runtime::mload<rev>);
            break;
        case MStore:
            emit.call_runtime(remaining_base_gas, monad::runtime::mstore<rev>);
            break;
        case MStore8:
            emit.call_runtime(remaining_base_gas, monad::runtime::mstore8<rev>);
            break;
        case SLoad:
            emit.call_runtime(remaining_base_gas, monad::runtime::sload<rev>);
            break;
        case SStore:
            emit.call_runtime(remaining_base_gas, monad::runtime::sstore<rev>);
            break;
        case Pc:
            emit.push(instr.pc());
            break;
        case MSize:
            emit.msize();
            break;
        case Gas:
            emit.gas(remaining_base_gas);
            break;
        case TLoad:
            emit.call_runtime(remaining_base_gas, monad::runtime::tload<rev>);
            break;
        case TStore:
            emit.call_runtime(remaining_base_gas, monad::runtime::tstore<rev>);
            break;
        case MCopy:
            emit.call_runtime(remaining_base_gas, monad::runtime::mcopy<rev>);
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
            switch (instr.index()) {
            case 0:
                emit.call_runtime(
                    remaining_base_gas, monad::runtime::log0<rev>);
                break;
            case 1:
                emit.call_runtime(
                    remaining_base_gas, monad::runtime::log1<rev>);
                break;
            case 2:
                emit.call_runtime(
                    remaining_base_gas, monad::runtime::log2<rev>);
                break;
            case 3:
                emit.call_runtime(
                    remaining_base_gas, monad::runtime::log3<rev>);
                break;
            case 4:
                emit.call_runtime(
                    remaining_base_gas, monad::runtime::log4<rev>);
                break;
            default:
                MONAD_COMPILER_ASSERT(false);
            }
            break;
        case Create:
            emit.call_runtime(remaining_base_gas, monad::runtime::create<rev>);
            break;
        case Call:
            emit.call_runtime(remaining_base_gas, monad::runtime::call<rev>);
            break;
        case CallCode:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::callcode<rev>);
            break;
        case DelegateCall:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::delegatecall<rev>);
            break;
        case Create2:
            emit.call_runtime(remaining_base_gas, monad::runtime::create2<rev>);
            break;
        case StaticCall:
            emit.call_runtime(
                remaining_base_gas, monad::runtime::staticcall<rev>);
            break;
        }
    }

    template <evmc_revision rev>
    void emit_instrs(Emitter &emit, Block const &block, int32_t instr_gas)
    {
        MONAD_COMPILER_ASSERT(instr_gas <= std::numeric_limits<int32_t>::max());
        int32_t remaining_base_gas = instr_gas;
        for (auto const &instr : block.instrs) {
            MONAD_COMPILER_DEBUG_ASSERT(
                remaining_base_gas >= instr.static_gas_cost());
            remaining_base_gas -= instr.static_gas_cost();
            emit_instr<rev>(emit, instr, remaining_base_gas);
        }
    }

    template <evmc_revision rev>
    void emit_terminator(Emitter &emit, Block const &block)
    {
        // Remaining block base gas is zero for terminator instruction,
        // because there are no more instructions left in the block.
        constexpr int32_t remaining_base_gas = 0;
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
            emit.call_runtime(
                remaining_base_gas, monad::runtime::selfdestruct<rev>);
            break;
        case InvalidInstruction:
            emit.invalid_instruction();
            break;
        }
    }

    void emit_gas_decrement(
        Emitter &emit, BasicBlocksIR const &ir, Block const &block,
        int32_t block_base_gas, int32_t *accumulated_base_gas)
    {
        if (ir.jump_dests().contains(block.offset)) {
            *accumulated_base_gas = 0;
            emit.gas_decrement_check_non_negative(block_base_gas + 1);
            return;
        }

        // Arbitrary gas threshold for when to emit gas check.
        // Needs to be big enough to make the gas check insignificant,
        // and small enough to avoid exploitation of the optimization.
        constexpr int32_t STATIC_GAS_CHECK_THRESHOLD = 1000;

        int32_t const acc = *accumulated_base_gas + block_base_gas;
        if (acc < STATIC_GAS_CHECK_THRESHOLD) {
            *accumulated_base_gas = acc;
            emit.gas_decrement_no_check(block_base_gas);
        }
        else {
            *accumulated_base_gas = 0;
            emit.gas_decrement_check_non_negative(block_base_gas);
        }
    }

    template <evmc_revision rev>
    entrypoint_t compile_local_stacks(
        asmjit::JitRuntime &rt, BasicBlocksIR const &ir, char const *asm_log)
    {
        Emitter emit{rt, ir.codesize, asm_log};
        for (auto const &[d, _] : ir.jump_dests()) {
            emit.add_jump_dest(d);
        }
        int32_t accumulated_base_gas = 0;
        for (Block const &block : ir.blocks()) {
            bool const can_enter_block = emit.begin_new_block(block);
            if (can_enter_block) {
                int32_t const base_gas = block_base_gas<rev>(block);
                emit_gas_decrement(
                    emit, ir, block, base_gas, &accumulated_base_gas);
                emit_instrs<rev>(emit, block, base_gas);
                emit_terminator<rev>(emit, block);
            }
        }
        return emit.finish_contract(rt);
    }

    template <evmc_revision Rev>
    entrypoint_t compile_contract(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        char const *asm_log)
    {
        auto ir = BasicBlocksIR(basic_blocks::make_ir<Rev>(contract));
        return compile_local_stacks<Rev>(rt, ir, asm_log);
    }
}

namespace monad::compiler::native
{
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev, char const *asm_log)
    {
        try {
            switch (rev) {
            case EVMC_FRONTIER:
                return ::compile_contract<EVMC_FRONTIER>(rt, contract, asm_log);
            case EVMC_HOMESTEAD:
                return ::compile_contract<EVMC_HOMESTEAD>(
                    rt, contract, asm_log);
            case EVMC_TANGERINE_WHISTLE:
                return ::compile_contract<EVMC_TANGERINE_WHISTLE>(
                    rt, contract, asm_log);
            case EVMC_SPURIOUS_DRAGON:
                return ::compile_contract<EVMC_SPURIOUS_DRAGON>(
                    rt, contract, asm_log);
            case EVMC_BYZANTIUM:
                return ::compile_contract<EVMC_BYZANTIUM>(
                    rt, contract, asm_log);
            case EVMC_CONSTANTINOPLE:
                return ::compile_contract<EVMC_CONSTANTINOPLE>(
                    rt, contract, asm_log);
            case EVMC_PETERSBURG:
                return ::compile_contract<EVMC_PETERSBURG>(
                    rt, contract, asm_log);
            case EVMC_ISTANBUL:
                return ::compile_contract<EVMC_ISTANBUL>(rt, contract, asm_log);
            case EVMC_BERLIN:
                return ::compile_contract<EVMC_BERLIN>(rt, contract, asm_log);
            case EVMC_LONDON:
                return ::compile_contract<EVMC_LONDON>(rt, contract, asm_log);
            case EVMC_PARIS:
                return ::compile_contract<EVMC_PARIS>(rt, contract, asm_log);
            case EVMC_SHANGHAI:
                return ::compile_contract<EVMC_SHANGHAI>(rt, contract, asm_log);
            case EVMC_CANCUN:
                return ::compile_contract<EVMC_CANCUN>(rt, contract, asm_log);
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
