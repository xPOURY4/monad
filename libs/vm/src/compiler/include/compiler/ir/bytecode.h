#pragma once

#include <intx/intx.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

using byte_offset = std::size_t;
using uint256_t = ::intx::uint256;

namespace monad::compiler
{

    struct Token
    {
        byte_offset token_offset;
        uint8_t token_opcode;
        uint256_t token_data; // only used by push
    };

    class BytecodeIR
    {
    public:
        BytecodeIR(std::vector<uint8_t> const &byte_code);
        std::vector<Token> tokens;
    };

    struct OpCodeInfo
    {
        std::string_view name;
        std::size_t num_args;
        int min_stack;
        bool increases_stack;
        int min_gas;
    };

    extern OpCodeInfo const opCodeInfo[];

}
