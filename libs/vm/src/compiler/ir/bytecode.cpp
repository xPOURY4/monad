#include <algorithm>
#include <compiler/ir/bytecode.h>

#include <intx/intx.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

namespace
{
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

namespace monad::compiler
{

    BytecodeIR::BytecodeIR(std::vector<uint8_t> const &byte_code)
    {
        byte_offset curr_offset = 0;
        while (curr_offset < byte_code.size()) {
            uint8_t const opcode = byte_code[curr_offset];
            std::size_t const n = opcode_info_table[opcode].num_args;
            byte_offset const opcode_offset = curr_offset;

            curr_offset++;

            tokens.emplace_back(
                opcode_offset,
                opcode,
                to_uint256_t(
                    n,
                    byte_code.size() - curr_offset,
                    &byte_code[curr_offset]));

            curr_offset += n;
        }
    }

    bool operator==(Token const &a, Token const &b)
    {
        return std::tie(a.token_offset, a.token_opcode, a.token_data) ==
               std::tie(b.token_offset, b.token_opcode, b.token_data);
    }

}
