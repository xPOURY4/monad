#include <compiler/ir/simple_llvm.h>

#include <llvm/IR/Module.h>

#include <cassert>
#include <iostream>
#include <memory>
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
