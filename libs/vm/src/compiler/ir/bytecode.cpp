#include <compiler/ir/bytecode.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>
#include <utils/uint256.h>

#include <initializer_list>
#include <intx/intx.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <tuple>
#include <vector>

namespace monad::compiler::bytecode
{
    BytecodeIR::BytecodeIR(std::initializer_list<uint8_t> byte_code)
        : BytecodeIR(std::span<uint8_t const>{byte_code})
    {
    }

    BytecodeIR::BytecodeIR(std::span<uint8_t const> byte_code)
    {
        codesize = static_cast<uint64_t>(byte_code.size());
        byte_offset curr_offset = 0;
        while (curr_offset < byte_code.size()) {
            uint8_t const opcode = byte_code[curr_offset];
            std::size_t const n = opcode_info_table[opcode].num_args;
            byte_offset const opcode_offset = curr_offset;

            curr_offset++;

            instructions.emplace_back(
                opcode_offset,
                opcode,
                utils::from_bytes(
                    n,
                    byte_code.size() - curr_offset,
                    &byte_code[curr_offset]));

            curr_offset += n;
        }
    }

    bool operator==(Instruction const &a, Instruction const &b)
    {
        return std::tie(a.offset, a.opcode, a.data) ==
               std::tie(b.offset, b.opcode, b.data);
    }

}
