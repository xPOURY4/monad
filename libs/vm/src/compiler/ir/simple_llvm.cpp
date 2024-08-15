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
        std::vector<llvm::BasicBlock *> const &blocks)
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

            auto *block_addr = llvm::BlockAddress::get(blocks[block_id]);
            phi->addIncoming(block_addr, case_block);
        }

        return table_fn;
    }
}

namespace monad::compiler
{
    SimpleLLVMIR::SimpleLLVMIR(InstructionIR const &instrs)
        : mod(std::make_unique<llvm::Module>("monad-evm", context()))
        , entry_point(build_entrypoint(*mod))
        , evm_blocks{}
    {
        assert(
            instrs.blocks.size() > 0 &&
            "Cannot compile program with no basic blocks");

        for (auto const &b : instrs.blocks) {
            evm_blocks.push_back(compile_block(b));
        }

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
}
