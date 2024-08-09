#ifndef MONAD_COMPILER_H
#define MONAD_COMPILER_H

#include <llvm/IR/Module.h>

#include <memory>

namespace monad::compiler
{

    std::unique_ptr<llvm::Module>
    compile_evm_bytecode(uint8_t const *code, size_t code_size);

}

#endif
