#pragma once

#include <compiler/compiler.h>
#include <compiler/ir/instruction.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <vector>

namespace monad::compiler
{

    class SimpleLLVMIR
    {
    public:
        SimpleLLVMIR(InstructionIR const &instrs);

        compile_result result() &&;

    private:
        std::unique_ptr<llvm::Module> mod;
        llvm::Function *entry_point;
        std::vector<std::pair<llvm::BasicBlock *, Block>> evm_blocks;

        llvm::GlobalVariable *stack;
        llvm::GlobalVariable *stack_pointer;

        llvm::Function *push;
        llvm::Function *pop;
        llvm::Function *jump_table;

        llvm::Function *build_push_function();
        llvm::Function *build_pop_function();

        llvm::BasicBlock *compile_block(Block const &b) const;
        void compile_block_terminators();

        llvm::CallInst *call_pop(llvm::BasicBlock *insert_at_end) const;
        llvm::CallInst *call_jump_table(
            llvm::Value *arg, llvm::BasicBlock *insert_at_end) const;

        llvm::IndirectBrInst *
        dynamic_jump(llvm::Value *dest, llvm::BasicBlock *insert_at_end) const;
    };

}
