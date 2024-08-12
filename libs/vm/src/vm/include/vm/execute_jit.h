#ifndef MONAD_VM_EXECUTE_JIT_H
#define MONAD_VM_EXECUTE_JIT_H

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Module.h>

#include <memory>

namespace monad::vm
{
    std::unique_ptr<llvm::ExecutionEngine>
    create_engine(std::unique_ptr<llvm::Module> mod);
}

#endif
