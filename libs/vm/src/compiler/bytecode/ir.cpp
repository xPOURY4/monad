#include "ir.h"
#include "intx/intx.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    uint256_t to_uint256_t(std::size_t const n, uint8_t const *src)
    {
        assert(n >= 0 && n <= 32);

        if (n == 0) {
            return 0;
        }

        uint8_t dst[32] = {};

        std::memcpy(dst, src, n);

        return intx::be::load<uint256_t>(dst);
    }
}

BytecodeIR::BytecodeIR(std::vector<uint8_t> const &byte_code)
{
    byte_offset curr_offset = 0;
    while (curr_offset < byte_code.size()) {
        uint8_t const opcode = byte_code[curr_offset];
        std::size_t const n = opCodeInfo[opcode].num_args;
        tokens.emplace_back(
            curr_offset, opcode, to_uint256_t(n, &byte_code[curr_offset]));
        curr_offset += 1 + n;
    }
}

#define UNKNOWN_OPCODE_INFO                                                    \
    {                                                                          \
        "UNKNOWN", 0, 0, false, 0                                              \
    }

OpCodeInfo const opCodeInfo[] = {
    {"STOP", 0, 0, false, 0}, // 0x00
    {"ADD", 0, 2, true, 3}, // 0x01
    {"MUL", 0, 2, true, 5}, // 0x02
    {"SUB", 0, 2, true, 3}, // 0x03
    {"DIV", 0, 2, true, 5}, // 0x04,
    {"SDIV", 0, 2, true, 5}, // 0x05,
    {"MOD", 0, 2, true, 5}, // 0x06,
    {"SMOD", 0, 2, true, 5}, // 0x07,
    {"ADDMOD", 0, 3, true, 8}, // 0x08,
    {"MULMOD", 0, 3, true, 8}, // 0x09,
    {"EXP", 0, 2, true, 10}, // 0x0A,
    {"SIGNEXTEND", 0, 2, true, 5}, // 0x0B,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"LT", 0, 2, true, 3}, // 0x10,
    {"GT", 0, 2, true, 3}, // 0x11,
    {"SLT", 0, 2, true, 3}, // 0x12,
    {"SGT", 0, 2, true, 3}, // 0x13,
    {"EQ", 0, 2, true, 3}, // 0x14,
    {"ISZERO", 0, 1, true, 3}, // 0x15,
    {"AND", 0, 2, true, 3}, // 0x16,
    {"OR", 0, 2, true, 3}, // 0x17,
    {"XOR", 0, 2, true, 3}, // 0x18,
    {"NOT", 0, 1, true, 3}, // 0x19,
    {"BYTE", 0, 2, true, 3}, // 0x1A,
    {"SHL", 0, 2, true, 3}, // 0x1B,
    {"SHR", 0, 2, true, 3}, // 0x1C,
    {"SAR", 0, 2, true, 3}, // 0x1D,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"SHA3", 0, 2, true, 30}, // 0x20,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"ADDRESS", 0, 0, true, 2}, // 0x30,
    {"BALANCE", 0, 1, true, 100}, // 0x31,
    {"ORIGIN", 0, 0, true, 2}, // 0x32,
    {"CALLER", 0, 0, true, 2}, // 0x33,
    {"CALLVALUE", 0, 0, true, 2}, // 0x34,
    {"CALLDATALOAD", 0, 1, true, 3}, // 0x35,
    {"CALLDATASIZE", 0, 0, true, 2}, // 0x36,
    {"CALLDATACOPY", 0, 3, false, 3}, // 0x37,
    {"CODESIZE", 0, 0, true, 2}, // 0x38,
    {"CODECOPY", 0, 3, false, 3}, // 0x39,
    {"GASPRICE", 0, 0, true, 2}, // 0x3A,
    {"EXTCODESIZE", 0, 1, true, 100}, // 0x3B,
    {"EXTCODECOPY", 0, 4, false, 100}, // 0x3C,
    {"RETURNDATASIZE", 0, 0, true, 2}, // 0x3D,
    {"RETURNDATACOPY", 0, 3, false, 3}, // 0x3E,
    {"EXTCODEHASH", 0, 1, true, 100}, // 0x3F,

    {"BLOCKHASH", 0, 1, true, 20}, // 0x40,
    {"COINBASE", 0, 0, true, 2}, // 0x41,
    {"TIMESTAMP", 0, 0, true, 2}, // 0x42,
    {"NUMBER", 0, 0, true, 2}, // 0x43,
    {"DIFFICULTY", 0, 0, true, 2}, // 0x44,
    {"GASLIMIT", 0, 0, true, 2}, // 0x45,
    {"CHAINID", 0, 0, true, 2}, // 0x46,
    {"SELFBALANCE", 0, 0, true, 5}, // 0x47,
    {"BASEFEE", 0, 0, true, 2}, // 0x48,
    {"BLOBHASH", 0, 1, true, 3}, // 0x49,
    {"BLOBBASEFEE", 0, 0, true, 2}, // 0x4A,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"POP", 0, 1, false, 2}, // 0x50,
    {"MLOAD", 0, 1, true, 3}, // 0x51,
    {"MSTORE", 0, 2, false, 3}, // 0x52,
    {"MSTORE8", 0, 2, false, 3}, // 0x53,
    {"SLOAD", 0, 1, true, 100}, // 0x54,
    {"SSTORE", 0, 2, false, 100}, // 0x55,
    {"JUMP", 0, 1, false, 8}, // 0x56,
    {"JUMPI", 0, 2, false, 10}, // 0x57,
    {"PC", 0, 0, true, 2}, // 0x58,
    {"MSIZE", 0, 0, true, 2}, // 0x59,
    {"GAS", 0, 0, true, 2}, // 0x5A,
    {"JUMPDEST", 0, 0, false, 1}, // 0x5B,
    {"TLOAD", 0, 1, true, 100}, // 0x5C,
    {"TSTORE", 0, 2, false, 100}, // 0x5D,
    {"MCOPY", 0, 3, false, 3}, // 0x5E,
    {"PUSH0", 0, 0, true, 2}, // 0x5F,

    {"PUSH1", 1, 0, true, 3}, // 0x60,
    {"PUSH2", 2, 0, true, 3}, // 0x61,
    {"PUSH3", 3, 0, true, 3}, // 0x62,
    {"PUSH4", 4, 0, true, 3}, // 0x63,
    {"PUSH5", 5, 0, true, 3}, // 0x64,
    {"PUSH6", 6, 0, true, 3}, // 0x65,
    {"PUSH7", 7, 0, true, 3}, // 0x66,
    {"PUSH8", 8, 0, true, 3}, // 0x67,
    {"PUSH9", 9, 0, true, 3}, // 0x68,
    {"PUSH10", 10, 0, true, 3}, // 0x69,
    {"PUSH11", 11, 0, true, 3}, // 0x6A,
    {"PUSH12", 12, 0, true, 3}, // 0x6B,
    {"PUSH13", 13, 0, true, 3}, // 0x6C,
    {"PUSH14", 14, 0, true, 3}, // 0x6D,
    {"PUSH15", 15, 0, true, 3}, // 0x6E,
    {"PUSH16", 16, 0, true, 3}, // 0x6F,

    {"PUSH17", 17, 0, true, 3}, // 0x70,
    {"PUSH18", 18, 0, true, 3}, // 0x71,
    {"PUSH19", 19, 0, true, 3}, // 0x72,
    {"PUSH20", 20, 0, true, 3}, // 0x73,
    {"PUSH21", 21, 0, true, 3}, // 0x74,
    {"PUSH22", 22, 0, true, 3}, // 0x75,
    {"PUSH23", 23, 0, true, 3}, // 0x76,
    {"PUSH24", 24, 0, true, 3}, // 0x77,
    {"PUSH25", 25, 0, true, 3}, // 0x78,
    {"PUSH26", 26, 0, true, 3}, // 0x79,
    {"PUSH27", 27, 0, true, 3}, // 0x7A,
    {"PUSH28", 28, 0, true, 3}, // 0x7B,
    {"PUSH29", 29, 0, true, 3}, // 0x7C,
    {"PUSH30", 30, 0, true, 3}, // 0x7D,
    {"PUSH31", 31, 0, true, 3}, // 0x7E,
    {"PUSH32", 32, 0, true, 3}, // 0x7F,

    {"DUP1", 0, 1, true, 3}, // 0x80,
    {"DUP2", 0, 2, true, 3}, // 0x81,
    {"DUP3", 0, 3, true, 3}, // 0x82,
    {"DUP4", 0, 4, true, 3}, // 0x83,
    {"DUP5", 0, 5, true, 3}, // 0x84,
    {"DUP6", 0, 6, true, 3}, // 0x85,
    {"DUP7", 0, 7, true, 3}, // 0x86,
    {"DUP8", 0, 8, true, 3}, // 0x87,
    {"DUP9", 0, 9, true, 3}, // 0x88,
    {"DUP10", 0, 10, true, 3}, // 0x89,
    {"DUP11", 0, 11, true, 3}, // 0x8A,
    {"DUP12", 0, 12, true, 3}, // 0x8B,
    {"DUP13", 0, 13, true, 3}, // 0x8C,
    {"DUP14", 0, 14, true, 3}, // 0x8D,
    {"DUP15", 0, 15, true, 3}, // 0x8E,
    {"DUP16", 0, 16, true, 3}, // 0x8F,

    {"SWAP1", 0, 1 + 1, false, 3}, // 0x90,
    {"SWAP2", 0, 1 + 2, false, 3}, // 0x91,
    {"SWAP3", 0, 1 + 3, false, 3}, // 0x92,
    {"SWAP4", 0, 1 + 4, false, 3}, // 0x93,
    {"SWAP5", 0, 1 + 5, false, 3}, // 0x94,
    {"SWAP6", 0, 1 + 6, false, 3}, // 0x95,
    {"SWAP7", 0, 1 + 7, false, 3}, // 0x96,
    {"SWAP8", 0, 1 + 8, false, 3}, // 0x97,
    {"SWAP9", 0, 1 + 9, false, 3}, // 0x98,
    {"SWAP10", 0, 1 + 10, false, 3}, // 0x99,
    {"SWAP11", 0, 1 + 11, false, 3}, // 0x9A,
    {"SWAP12", 0, 1 + 12, false, 3}, // 0x9B,
    {"SWAP13", 0, 1 + 13, false, 3}, // 0x9C,
    {"SWAP14", 0, 1 + 14, false, 3}, // 0x9D,
    {"SWAP15", 0, 1 + 15, false, 3}, // 0x9E,
    {"SWAP16", 0, 1 + 16, false, 3}, // 0x9F,

    {"LOG0", 0, 2 + 0, false, 375}, // 0xA0,
    {"LOG1", 0, 2 + 1, false, 750}, // 0xA1,
    {"LOG2", 0, 2 + 2, false, 1125}, // 0xA2,
    {"LOG3", 0, 2 + 3, false, 1500}, // 0xA3,
    {"LOG4", 0, 2 + 4, false, 1875}, // 0xA4,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xB0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xC0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xD0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    UNKNOWN_OPCODE_INFO, // 0xE0
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,

    {"CREATE", 0, 3, true, 32000}, // 0xF0,
    {"CALL", 0, 7, true, 100}, // 0xF1,
    {"CALLCODE", 0, 7, true, 100}, // 0xF2,
    {"RETURN", 0, 2, false, 0}, // 0xF3,
    {"DELEGATECALL", 0, 6, true, 100}, // 0xF4,
    {"CREATE2", 0, 4, true, 3200}, // 0xF5,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    {"STATICCALL", 0, 6, true, 100}, // 0xFA,
    UNKNOWN_OPCODE_INFO,
    UNKNOWN_OPCODE_INFO,
    {"REVERT", 0, 2, false, 0}, // 0xFD,
    UNKNOWN_OPCODE_INFO,
    {"SELFDESTRUCT", 0, 1, false, 5000} // 0xFF,
};
