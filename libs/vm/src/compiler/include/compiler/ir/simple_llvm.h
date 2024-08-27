#pragma once

#include <compiler/compiler.h>
#include <compiler/ir/basic_blocks.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <vector>

namespace monad::compiler
{

    class SimpleLLVMIR
    {
    public:
        SimpleLLVMIR(BasicBlocksIR const &instrs);

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

        llvm::BasicBlock *entry;
        llvm::BasicBlock *stop;
        llvm::BasicBlock *revert;
        llvm::BasicBlock *self_destruct;
        llvm::BasicBlock *return_;

        llvm::Function *build_push_function();
        llvm::Function *build_pop_function();

        llvm::BasicBlock *compile_block(Block const &b) const;
        void compile_block_terminators();

        void compile_instruction(
            llvm::IRBuilder<> &b, bytecode::Instruction const &inst) const;

        llvm::CallInst *call_pop() const;
        llvm::CallInst *call_push(uint256_t immediate) const;
        llvm::CallInst *call_jump_table(llvm::Value *arg) const;

        llvm::CallInst *call_runtime_function(
            std::string const &name, llvm::Value *interface,
            llvm::Value *extra = nullptr) const;

        llvm::IndirectBrInst *dynamic_jump(llvm::Value *dest) const;
    };

}
