#pragma once

#include <compiler/compiler.h>
#include <compiler/ir/instruction.h>

#include <llvm/IR/BasicBlock.h>
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
        std::vector<llvm::BasicBlock *> evm_blocks;

        llvm::GlobalVariable *stack;
        llvm::GlobalVariable *stack_pointer;

        llvm::BasicBlock *compile_block(Block const &b) const;
    };

}
