#include <compiler/ir/instruction.h>
#include <compiler/opcodes.h>

#include <cstdint>
#include <tuple>

namespace monad::compiler::basic_blocks
{

    bool operator==(Instruction const &a, Instruction const &b)
    {
        return std::tie(a.code, a.index, a.operand) ==
               std::tie(b.code, b.index, b.operand);
    }

    OpCodeInfo const &Instruction::info() const
    {
        switch (code) {
        case InstructionCode::Push:
            return opcode_info_table[PUSH0 + index];
        case InstructionCode::Dup:
            return opcode_info_table[DUP1 + index - 1];
        case InstructionCode::Swap:
            return opcode_info_table[SWAP1 + index - 1];
        default:
            return opcode_info_table[static_cast<uint8_t>(code)];
        }
    }

}
