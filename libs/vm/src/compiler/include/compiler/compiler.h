#pragma once

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include <memory>

namespace monad::compiler
{
    namespace constants
    {
        inline constexpr auto contract_entrypoint = "monad_evm_contract_main";
        inline constexpr auto jump_table = "monad_evm_jump_table";
    }

    struct compile_result
    {
        std::unique_ptr<llvm::Module> mod;
        llvm::Function *entrypoint;
    };

    llvm::LLVMContext &context();

    compile_result compile_evm_bytecode(uint8_t const *code, size_t code_size);
}
