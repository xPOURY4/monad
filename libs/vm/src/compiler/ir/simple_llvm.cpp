#include <compiler/ir/simple_llvm.h>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <cassert>
#include <format>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <utility>

namespace
{
    using namespace monad::compiler;

    llvm::FunctionType *contract_entrypoint_type()
    {
        auto *void_ty = llvm::Type::getVoidTy(context());
        auto *ptr_ty = llvm::PointerType::getUnqual(context());

        return llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty}, false);
    }

    llvm::Function *build_entrypoint(llvm::Module &mod)
    {
        return llvm::Function::Create(
            contract_entrypoint_type(),
            llvm::GlobalValue::LinkageTypes::ExternalLinkage,
            constants::contract_entrypoint,
            mod);
    }

    /**
     * Compile a slow dynamic jump table function based on the program's
     * JUMPDEST instructions.
     *
     * This function takes a single argument representing the byte offset into
     * the original program that should be jumped to, and returns a block
     * address constant that can be indirectly branched to. If the argument
     * offset was not a JUMPDEST instruction in the original program, then the
     * returned block address is null.
     */
    llvm::Function *build_slow_jump_table(
        llvm::Module &mod,
        std::unordered_map<byte_offset, block_id> const &jump_table,
        std::vector<std::pair<llvm::BasicBlock *, Block>> const &blocks)
    {
        auto *block_address_ty = llvm::PointerType::getUnqual(context());
        auto *block_id_ty =
            llvm::IntegerType::get(context(), sizeof(block_id) * CHAR_BIT);

        auto *table_fn_ty =
            llvm::FunctionType::get(block_address_ty, {block_id_ty}, false);

        auto *table_fn = llvm::Function::Create(
            table_fn_ty,
            llvm::GlobalValue::LinkageTypes::InternalLinkage,
            constants::jump_table,
            mod);

        auto *entry = llvm::BasicBlock::Create(context(), "entry", table_fn);
        auto *error = llvm::BasicBlock::Create(context(), "error", table_fn);
        auto *exit = llvm::BasicBlock::Create(context(), "exit", table_fn);

        auto b = llvm::IRBuilder(error);
        b.CreateRet(llvm::ConstantPointerNull::get(block_address_ty));

        b.SetInsertPoint(exit);
        auto *phi = b.CreatePHI(block_address_ty, 0);
        b.CreateRet(phi);

        b.SetInsertPoint(entry);

        auto *offset_arg = table_fn->getArg(0);
        auto *switch_table = b.CreateSwitch(offset_arg, error);

        for (auto const &[offset, block_id] : jump_table) {
            assert(block_id < blocks.size() && "Invalid block ID");

            auto *case_block = llvm::BasicBlock::Create(
                context(), std::format("jumpdest_{}", offset), table_fn);
            b.SetInsertPoint(case_block);
            b.CreateBr(exit);

            auto *offset_const = llvm::ConstantInt::get(block_id_ty, offset);
            switch_table->addCase(offset_const, case_block);

            auto [llvm_block, _] = blocks[block_id];
            auto *block_addr = llvm::BlockAddress::get(llvm_block);
            phi->addIncoming(block_addr, case_block);
        }

        return table_fn;
    }

    llvm::IntegerType *word_type()
    {
        return llvm::IntegerType::get(context(), 256);
    }

    llvm::IntegerType *stack_pointer_type()
    {
        return llvm::IntegerType::get(context(), 16);
    }

    llvm::GlobalVariable *build_evm_stack(llvm::Module &mod)
    {
        constexpr auto stack_size = 1024;

        auto *stack_type = llvm::ArrayType::get(word_type(), stack_size);

        auto *stack = new llvm::GlobalVariable(
            stack_type,
            false,
            llvm::GlobalValue::LinkageTypes::InternalLinkage,
            llvm::ConstantAggregateZero::get(stack_type),
            "evm_stack");

        mod.insertGlobalVariable(stack);
        return stack;
    }

    llvm::GlobalVariable *build_evm_stack_pointer(llvm::Module &mod)
    {
        auto *stack_ptr = new llvm::GlobalVariable(
            stack_pointer_type(),
            false,
            llvm::GlobalValue::LinkageTypes::InternalLinkage,
            llvm::ConstantInt::get(stack_pointer_type(), 0),
            "evm_stack_pointer");

        mod.insertGlobalVariable(stack_ptr);
        return stack_ptr;
    }
}

namespace monad::compiler
{
    SimpleLLVMIR::SimpleLLVMIR(InstructionIR const &instrs)
        : mod(std::make_unique<llvm::Module>("monad-evm", context()))
        , entry_point(build_entrypoint(*mod))
        , evm_blocks{}
        , stack(build_evm_stack(*mod))
        , stack_pointer(build_evm_stack_pointer(*mod))
    {
        assert(
            instrs.blocks.size() > 0 &&
            "Cannot compile program with no basic blocks");

        for (auto const &b : instrs.blocks) {
            evm_blocks.emplace_back(compile_block(b), b);
        }
        compile_block_terminators();

        build_slow_jump_table(*mod, instrs.jumpdests, evm_blocks);
    }

    compile_result SimpleLLVMIR::result() &&
    {
        return {std::move(mod), entry_point};
    }

    llvm::BasicBlock *SimpleLLVMIR::compile_block(Block const &b) const
    {
        (void)b;
        auto *block = llvm::BasicBlock::Create(context(), "", entry_point);
        return block;
    }

    void SimpleLLVMIR::compile_block_terminators()
    {
        for (auto const &[llvm_block, original_block] : evm_blocks) {
            switch (original_block.terminator) {
            case Terminator::JumpDest:
                llvm::BranchInst::Create(
                    evm_blocks[original_block.fallthrough_dest].first,
                    llvm_block);
                break;

            default:
                break;
            }
        }
    }
}
