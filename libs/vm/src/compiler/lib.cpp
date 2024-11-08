#include <compiler/compiler.h>
#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace monad::compiler
{
    compile_result compile_evm_bytecode(uint8_t const *code, size_t code_size)
    {
        auto program = std::vector(code, code + code_size);
        auto bytecode = bytecode::BytecodeIR(program);
        auto blocks = basic_blocks::BasicBlocksIR(program);

        (void)blocks;
        return {};
    }
}
