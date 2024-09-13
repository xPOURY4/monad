#include <compiler/compiler.h>
#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/simple_llvm.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
    // For the purposes of an initial prototype, let's assume that we are only
    // ever code-generating on a single thread, and can therefore just create a
    // static-storage context that will live for the program's entire lifetime.
    // This will need to be revisited in the future if we ever do anything more
    // sophisticated inside the compiler.
    llvm::LLVMContext compiler_context;
}

namespace monad::compiler
{
    llvm::LLVMContext &context()
    {
        return ::compiler_context;
    }

    compile_result compile_evm_bytecode(uint8_t const *code, size_t code_size)
    {
        auto program = std::vector(code, code + code_size);
        auto bytecode = bytecode::BytecodeIR(program);
        auto blocks = basic_blocks::BasicBlocksIR(program);
        auto llvm = SimpleLLVMIR(blocks);

        return std::move(llvm).result();
    }
}
