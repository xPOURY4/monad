#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/compiler/ir/instruction.hpp>
#include <monad/compiler/ir/x86.hpp>
#include <monad/compiler/ir/x86/emitter.hpp>
#include <monad/utils/assert.hpp>

#include <evmc/evmc.h>

#include <asmjit/core/jitruntime.h>

#include <cstdint>
#include <iostream>
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
            emit.mul<rev>(remaining_base_gas);
            break;
        case Sub:
            emit.sub();
            break;
        case Div:
            emit.udiv<rev>(remaining_base_gas);
            break;
        case SDiv:
            emit.sdiv<rev>(remaining_base_gas);
            break;
        case Mod:
            emit.umod<rev>(remaining_base_gas);
            break;
        case SMod:
            emit.smod<rev>(remaining_base_gas);
            break;
        case AddMod:
            emit.addmod<rev>(remaining_base_gas);
            break;
        case MulMod:
            emit.mulmod<rev>(remaining_base_gas);
            break;
        case Exp:
            emit.exp<rev>(remaining_base_gas);
            break;
        case SignExtend:
            emit.signextend();
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
            emit.sha3<rev>(remaining_base_gas);
            break;
        case Address:
            emit.address();
            break;
        case Balance:
            emit.balance<rev>(remaining_base_gas);
            break;
        case Origin:
            emit.origin();
            break;
        case Caller:
            emit.caller();
            break;
        case CallValue:
            emit.callvalue();
            break;
        case CallDataLoad:
            emit.calldataload<rev>(remaining_base_gas);
            break;
        case CallDataSize:
            emit.calldatasize();
            break;
        case CallDataCopy:
            emit.calldatacopy<rev>(remaining_base_gas);
            break;
        case CodeSize:
            emit.codesize();
            break;
        case CodeCopy:
            emit.codecopy<rev>(remaining_base_gas);
            break;
        case GasPrice:
            emit.gasprice();
            break;
        case ExtCodeSize:
            emit.extcodesize<rev>(remaining_base_gas);
            break;
        case ExtCodeCopy:
            emit.extcodecopy<rev>(remaining_base_gas);
            break;
        case ReturnDataSize:
            emit.returndatasize();
            break;
        case ReturnDataCopy:
            emit.returndatacopy<rev>(remaining_base_gas);
            break;
        case ExtCodeHash:
            emit.extcodehash<rev>(remaining_base_gas);
            break;
        case BlockHash:
            emit.blockhash<rev>(remaining_base_gas);
            break;
        case Coinbase:
            emit.coinbase();
            break;
        case Timestamp:
            emit.timestamp();
            break;
        case Number:
            emit.number();
            break;
        case Difficulty:
            emit.prevrandao();
            break;
        case GasLimit:
            emit.gaslimit();
            break;
        case ChainId:
            emit.chainid();
            break;
        case SelfBalance:
            emit.selfbalance<rev>(remaining_base_gas);
            break;
        case BaseFee:
            emit.basefee();
            break;
        case BlobHash:
            emit.blobhash<rev>(remaining_base_gas);
            break;
        case BlobBaseFee:
            emit.blobbasefee();
            break;
        case Pop:
            emit.pop();
            break;
        case MLoad:
            emit.mload<rev>(remaining_base_gas);
            break;
        case MStore:
            emit.mstore<rev>(remaining_base_gas);
            break;
        case MStore8:
            emit.mstore8<rev>(remaining_base_gas);
            break;
        case SLoad:
            emit.sload<rev>(remaining_base_gas);
            break;
        case SStore:
            emit.sstore<rev>(remaining_base_gas);
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
            emit.tload<rev>(remaining_base_gas);
            break;
        case TStore:
            emit.tstore<rev>(remaining_base_gas);
            break;
        case MCopy:
            emit.mcopy<rev>(remaining_base_gas);
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
                emit.log0<rev>(remaining_base_gas);
                break;
            case 1:
                emit.log1<rev>(remaining_base_gas);
                break;
            case 2:
                emit.log2<rev>(remaining_base_gas);
                break;
            case 3:
                emit.log3<rev>(remaining_base_gas);
                break;
            case 4:
                emit.log4<rev>(remaining_base_gas);
                break;
            default:
                MONAD_COMPILER_ASSERT(false);
            }
            break;
        case Create:
            emit.create<rev>(remaining_base_gas);
            break;
        case Call:
            emit.call<rev>(remaining_base_gas);
            break;
        case CallCode:
            emit.callcode<rev>(remaining_base_gas);
            break;
        case DelegateCall:
            emit.delegatecall<rev>(remaining_base_gas);
            break;
        case Create2:
            emit.create2<rev>(remaining_base_gas);
            break;
        case StaticCall:
            emit.staticcall<rev>(remaining_base_gas);
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
            emit.selfdestruct<rev>(remaining_base_gas);
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
    entrypoint_t compile_basic_blocks(
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
        return compile_basic_blocks<Rev>(rt, ir, asm_log);
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
            std::cerr << "X86 emitter error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }
}
