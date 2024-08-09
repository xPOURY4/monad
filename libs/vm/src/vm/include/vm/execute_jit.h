#ifndef MONAD_VM_EXECUTE_JIT_H
#define MONAD_VM_EXECUTE_JIT_H

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Module.h>

#include <expected>
#include <memory>

namespace monad::vm
{
    std::expected<std::unique_ptr<llvm::ExecutionEngine>, std::string>
    create_engine(std::unique_ptr<llvm::Module> mod);
}

#endif
