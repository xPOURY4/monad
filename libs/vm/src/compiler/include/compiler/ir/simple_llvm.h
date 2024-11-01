#pragma once

#include <compiler/compiler.h>
#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>

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
        SimpleLLVMIR(basic_blocks::BasicBlocksIR const &instrs);

        compile_result result() &&;

    private:
        std::unique_ptr<llvm::Module> mod;
        llvm::Function *entry_point;
        std::vector<std::pair<llvm::BasicBlock *, basic_blocks::Block>>
            evm_blocks;

        llvm::GlobalVariable *stack;
        llvm::GlobalVariable *stack_pointer;
        llvm::GlobalVariable *gas_left;

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

        llvm::BasicBlock *compile_block(basic_blocks::Block const &b) const;
        void compile_block_terminators();

        void compile_instruction(
            llvm::IRBuilder<> &b, basic_blocks::Instruction const &inst) const;

        void spend_static_gas(llvm::IRBuilder<> &b, uint64_t gas) const;

        llvm::CallInst *call_pop() const;
        llvm::CallInst *call_push(llvm::Value *val) const;
        llvm::CallInst *call_push(uint256_t immediate) const;
        llvm::CallInst *call_jump_table(llvm::Value *arg) const;

        llvm::CallInst *call_stop(llvm::Value *interface) const;
        void call_sstore(
            llvm::IRBuilder<> &b, llvm::Value *interface, llvm::Value *key,
            llvm::Value *val) const;

        llvm::IndirectBrInst *dynamic_jump(llvm::Value *dest) const;
    };

}
