#ifndef MONAD_COMPILER_H
#define MONAD_COMPILER_H

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include <memory>

namespace monad::compiler
{
    namespace constants
    {
        constexpr auto contract_entrypoint = "monad_evm_contract_main";
    }

    std::unique_ptr<llvm::Module>
    compile_evm_bytecode(uint8_t const *code, size_t code_size);

    llvm::FunctionType *contract_entrypoint_type();
    llvm::Function *build_entrypoint(llvm::Module &mod);
}

#endif
