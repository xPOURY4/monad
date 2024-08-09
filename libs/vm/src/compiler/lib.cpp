#include <compiler/compiler.h>

namespace monad::compiler
{

    std::unique_ptr<llvm::Module>
    compile_evm_bytecode(uint8_t const *code, size_t code_size)
    {
        (void)code;
        (void)code_size;
        return nullptr;
    }

}
