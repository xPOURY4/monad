#include <compiler/compiler.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace
{
    // For the purposes of an initial prototype, let's assume that we are only
    // ever code-generating on a single thread, and can therefore just create a
    // static-storage context that will live for the program's entire lifetime.
    // This will need to be revisited in the future if we ever do anything more
    // sophisticated inside the compiler.
    llvm::LLVMContext context;

    // This function is a stub that we should remove once the compiler actually
    // starts to work properly end-to-end; it's just here for the purposes of
    // making the smallest possible implementation that doesn't crash when we
    // try to execute it.
    void build_no_op_return(llvm::Function &func)
    {
        auto *entry_block = llvm::BasicBlock::Create(context, "entry", &func);
        auto b = llvm::IRBuilder(entry_block);
        b.CreateRetVoid();
    }
}

namespace monad::compiler
{

    compile_result compile_evm_bytecode(uint8_t const *code, size_t code_size)
    {
        (void)code;
        (void)code_size;

        auto mod = std::make_unique<llvm::Module>("monad-evm", context);

        auto *entrypoint = build_entrypoint(*mod);
        build_no_op_return(*entrypoint);

        return {std::move(mod), entrypoint};
    }

    llvm::FunctionType *contract_entrypoint_type()
    {
        auto *void_ty = llvm::Type::getVoidTy(context);
        auto *ptr_ty = llvm::PointerType::getUnqual(context);

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
