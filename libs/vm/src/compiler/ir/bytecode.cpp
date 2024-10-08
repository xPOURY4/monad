#include <compiler/ir/bytecode.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <intx/intx.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

namespace
{
    using namespace monad::compiler;

    /**
     * Parse a range of raw bytes with length `n` into a 256-bit big-endian word
     * value.
     *
     * If there are fewer than `n` bytes remaining in the source data (that is,
     * `remaining < n`), then treat the input as if it had been padded to the
     * right with zero bytes.
     */
    uint256_t to_uint256_t(
        std::size_t const n, std::size_t const remaining, uint8_t const *src)
    {
        assert(n <= 32);

        if (n == 0) {
            return 0;
        }

        uint8_t dst[32] = {};

        std::memcpy(&dst[32 - n], src, std::min(n, remaining));

        return intx::be::load<uint256_t>(dst);
    }
}

namespace monad::compiler::bytecode
{

    BytecodeIR::BytecodeIR(std::vector<uint8_t> const &byte_code)
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
                to_uint256_t(
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
