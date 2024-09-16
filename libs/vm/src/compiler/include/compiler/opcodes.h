#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string_view>

namespace monad::compiler
{
    /**
     * Details of how an individual EVM opcode affects VM state when executed.
     */
    struct OpCodeInfo
    {
        /**
         * The human-readable (disassembled) form of the opcode.
         */
        std::string_view name;

        /**
         * The number of argument bytes that follow this opcode in a binary EVM
         * program.
         *
         * This value is 0 for all instructions other than the `PUSHN` family,
         * each of which expects N bytes to follow.
         */
        std::size_t num_args;

        /**
         * The minimum EVM stack size required to execute this instruction.
         */
        std::size_t min_stack;

        /**
         * Whether the EVM stack size increases after executing this
         * instruction.
         */
        bool increases_stack;

        /**
         * Minimum static gas required to execute this instruction.
         *
         * Some instructions may also consume additional dynamic gas depending
         * on run-time properties (e.g. memory expansion or storage costs).
         */
        std::uint64_t min_gas;
    };

    /**
     * Placeholder value representing an opcode value not currently used by the
     * EVM specification.
     */
    inline constexpr auto unknown_opcode_info =
        OpCodeInfo{"UNKNOWN", 0, 0, false, 0};

    /**
     * Lookup table of opcode info for each possible 1-byte opcode value.
     *
     * Some bytes do not correspond to an EVM instruction; looking those bytes
     * up in this table produces a placeholder value.
     */
    inline constexpr auto opcode_info_table = std::array{
        OpCodeInfo{"STOP", 0, 0, false, 0}, // 0x00
        OpCodeInfo{"ADD", 0, 2, true, 3}, // 0x01
        OpCodeInfo{"MUL", 0, 2, true, 5}, // 0x02
        OpCodeInfo{"SUB", 0, 2, true, 3}, // 0x03
        OpCodeInfo{"DIV", 0, 2, true, 5}, // 0x04,
        OpCodeInfo{"SDIV", 0, 2, true, 5}, // 0x05,
        OpCodeInfo{"MOD", 0, 2, true, 5}, // 0x06,
        OpCodeInfo{"SMOD", 0, 2, true, 5}, // 0x07,
        OpCodeInfo{"ADDMOD", 0, 3, true, 8}, // 0x08,
        OpCodeInfo{"MULMOD", 0, 3, true, 8}, // 0x09,
        OpCodeInfo{"EXP", 0, 2, true, 10}, // 0x0A,
        OpCodeInfo{"SIGNEXTEND", 0, 2, true, 5}, // 0x0B,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        OpCodeInfo{"LT", 0, 2, true, 3}, // 0x10,
        OpCodeInfo{"GT", 0, 2, true, 3}, // 0x11,
        OpCodeInfo{"SLT", 0, 2, true, 3}, // 0x12,
        OpCodeInfo{"SGT", 0, 2, true, 3}, // 0x13,
        OpCodeInfo{"EQ", 0, 2, true, 3}, // 0x14,
        OpCodeInfo{"ISZERO", 0, 1, true, 3}, // 0x15,
        OpCodeInfo{"AND", 0, 2, true, 3}, // 0x16,
        OpCodeInfo{"OR", 0, 2, true, 3}, // 0x17,
        OpCodeInfo{"XOR", 0, 2, true, 3}, // 0x18,
        OpCodeInfo{"NOT", 0, 1, true, 3}, // 0x19,
        OpCodeInfo{"BYTE", 0, 2, true, 3}, // 0x1A,
        OpCodeInfo{"SHL", 0, 2, true, 3}, // 0x1B,
        OpCodeInfo{"SHR", 0, 2, true, 3}, // 0x1C,
        OpCodeInfo{"SAR", 0, 2, true, 3}, // 0x1D,
        unknown_opcode_info,
        unknown_opcode_info,

        OpCodeInfo{"SHA3", 0, 2, true, 30}, // 0x20,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        OpCodeInfo{"ADDRESS", 0, 0, true, 2}, // 0x30,
        OpCodeInfo{"BALANCE", 0, 1, true, 100}, // 0x31,
        OpCodeInfo{"ORIGIN", 0, 0, true, 2}, // 0x32,
        OpCodeInfo{"CALLER", 0, 0, true, 2}, // 0x33,
        OpCodeInfo{"CALLVALUE", 0, 0, true, 2}, // 0x34,
        OpCodeInfo{"CALLDATALOAD", 0, 1, true, 3}, // 0x35,
        OpCodeInfo{"CALLDATASIZE", 0, 0, true, 2}, // 0x36,
        OpCodeInfo{"CALLDATACOPY", 0, 3, false, 3}, // 0x37,
        OpCodeInfo{"CODESIZE", 0, 0, true, 2}, // 0x38,
        OpCodeInfo{"CODECOPY", 0, 3, false, 3}, // 0x39,
        OpCodeInfo{"GASPRICE", 0, 0, true, 2}, // 0x3A,
        OpCodeInfo{"EXTCODESIZE", 0, 1, true, 100}, // 0x3B,
        OpCodeInfo{"EXTCODECOPY", 0, 4, false, 100}, // 0x3C,
        OpCodeInfo{"RETURNDATASIZE", 0, 0, true, 2}, // 0x3D,
        OpCodeInfo{"RETURNDATACOPY", 0, 3, false, 3}, // 0x3E,
        OpCodeInfo{"EXTCODEHASH", 0, 1, true, 100}, // 0x3F,

        OpCodeInfo{"BLOCKHASH", 0, 1, true, 20}, // 0x40,
        OpCodeInfo{"COINBASE", 0, 0, true, 2}, // 0x41,
        OpCodeInfo{"TIMESTAMP", 0, 0, true, 2}, // 0x42,
        OpCodeInfo{"NUMBER", 0, 0, true, 2}, // 0x43,
        OpCodeInfo{"DIFFICULTY", 0, 0, true, 2}, // 0x44,
        OpCodeInfo{"GASLIMIT", 0, 0, true, 2}, // 0x45,
        OpCodeInfo{"CHAINID", 0, 0, true, 2}, // 0x46,
        OpCodeInfo{"SELFBALANCE", 0, 0, true, 5}, // 0x47,
        OpCodeInfo{"BASEFEE", 0, 0, true, 2}, // 0x48,
        OpCodeInfo{"BLOBHASH", 0, 1, true, 3}, // 0x49,
        OpCodeInfo{"BLOBBASEFEE", 0, 0, true, 2}, // 0x4A,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        OpCodeInfo{"POP", 0, 1, false, 2}, // 0x50,
        OpCodeInfo{"MLOAD", 0, 1, true, 3}, // 0x51,
        OpCodeInfo{"MSTORE", 0, 2, false, 3}, // 0x52,
        OpCodeInfo{"MSTORE8", 0, 2, false, 3}, // 0x53,
        OpCodeInfo{"SLOAD", 0, 1, true, 100}, // 0x54,
        OpCodeInfo{"SSTORE", 0, 2, false, 100}, // 0x55,
        OpCodeInfo{"JUMP", 0, 1, false, 8}, // 0x56,
        OpCodeInfo{"JUMPI", 0, 2, false, 10}, // 0x57,
        OpCodeInfo{"PC", 0, 0, true, 2}, // 0x58,
        OpCodeInfo{"MSIZE", 0, 0, true, 2}, // 0x59,
        OpCodeInfo{"GAS", 0, 0, true, 2}, // 0x5A,
        OpCodeInfo{"JUMPDEST", 0, 0, false, 1}, // 0x5B,
        OpCodeInfo{"TLOAD", 0, 1, true, 100}, // 0x5C,
        OpCodeInfo{"TSTORE", 0, 2, false, 100}, // 0x5D,
        OpCodeInfo{"MCOPY", 0, 3, false, 3}, // 0x5E,
        OpCodeInfo{"PUSH0", 0, 0, true, 2}, // 0x5F,

        OpCodeInfo{"PUSH1", 1, 0, true, 3}, // 0x60,
        OpCodeInfo{"PUSH2", 2, 0, true, 3}, // 0x61,
        OpCodeInfo{"PUSH3", 3, 0, true, 3}, // 0x62,
        OpCodeInfo{"PUSH4", 4, 0, true, 3}, // 0x63,
        OpCodeInfo{"PUSH5", 5, 0, true, 3}, // 0x64,
        OpCodeInfo{"PUSH6", 6, 0, true, 3}, // 0x65,
        OpCodeInfo{"PUSH7", 7, 0, true, 3}, // 0x66,
        OpCodeInfo{"PUSH8", 8, 0, true, 3}, // 0x67,
        OpCodeInfo{"PUSH9", 9, 0, true, 3}, // 0x68,
        OpCodeInfo{"PUSH10", 10, 0, true, 3}, // 0x69,
        OpCodeInfo{"PUSH11", 11, 0, true, 3}, // 0x6A,
        OpCodeInfo{"PUSH12", 12, 0, true, 3}, // 0x6B,
        OpCodeInfo{"PUSH13", 13, 0, true, 3}, // 0x6C,
        OpCodeInfo{"PUSH14", 14, 0, true, 3}, // 0x6D,
        OpCodeInfo{"PUSH15", 15, 0, true, 3}, // 0x6E,
        OpCodeInfo{"PUSH16", 16, 0, true, 3}, // 0x6F,

        OpCodeInfo{"PUSH17", 17, 0, true, 3}, // 0x70,
        OpCodeInfo{"PUSH18", 18, 0, true, 3}, // 0x71,
        OpCodeInfo{"PUSH19", 19, 0, true, 3}, // 0x72,
        OpCodeInfo{"PUSH20", 20, 0, true, 3}, // 0x73,
        OpCodeInfo{"PUSH21", 21, 0, true, 3}, // 0x74,
        OpCodeInfo{"PUSH22", 22, 0, true, 3}, // 0x75,
        OpCodeInfo{"PUSH23", 23, 0, true, 3}, // 0x76,
        OpCodeInfo{"PUSH24", 24, 0, true, 3}, // 0x77,
        OpCodeInfo{"PUSH25", 25, 0, true, 3}, // 0x78,
        OpCodeInfo{"PUSH26", 26, 0, true, 3}, // 0x79,
        OpCodeInfo{"PUSH27", 27, 0, true, 3}, // 0x7A,
        OpCodeInfo{"PUSH28", 28, 0, true, 3}, // 0x7B,
        OpCodeInfo{"PUSH29", 29, 0, true, 3}, // 0x7C,
        OpCodeInfo{"PUSH30", 30, 0, true, 3}, // 0x7D,
        OpCodeInfo{"PUSH31", 31, 0, true, 3}, // 0x7E,
        OpCodeInfo{"PUSH32", 32, 0, true, 3}, // 0x7F,

        OpCodeInfo{"DUP1", 0, 1, true, 3}, // 0x80,
        OpCodeInfo{"DUP2", 0, 2, true, 3}, // 0x81,
        OpCodeInfo{"DUP3", 0, 3, true, 3}, // 0x82,
        OpCodeInfo{"DUP4", 0, 4, true, 3}, // 0x83,
        OpCodeInfo{"DUP5", 0, 5, true, 3}, // 0x84,
        OpCodeInfo{"DUP6", 0, 6, true, 3}, // 0x85,
        OpCodeInfo{"DUP7", 0, 7, true, 3}, // 0x86,
        OpCodeInfo{"DUP8", 0, 8, true, 3}, // 0x87,
        OpCodeInfo{"DUP9", 0, 9, true, 3}, // 0x88,
        OpCodeInfo{"DUP10", 0, 10, true, 3}, // 0x89,
        OpCodeInfo{"DUP11", 0, 11, true, 3}, // 0x8A,
        OpCodeInfo{"DUP12", 0, 12, true, 3}, // 0x8B,
        OpCodeInfo{"DUP13", 0, 13, true, 3}, // 0x8C,
        OpCodeInfo{"DUP14", 0, 14, true, 3}, // 0x8D,
        OpCodeInfo{"DUP15", 0, 15, true, 3}, // 0x8E,
        OpCodeInfo{"DUP16", 0, 16, true, 3}, // 0x8F,

        OpCodeInfo{"SWAP1", 0, 1 + 1, false, 3}, // 0x90,
        OpCodeInfo{"SWAP2", 0, 1 + 2, false, 3}, // 0x91,
        OpCodeInfo{"SWAP3", 0, 1 + 3, false, 3}, // 0x92,
        OpCodeInfo{"SWAP4", 0, 1 + 4, false, 3}, // 0x93,
        OpCodeInfo{"SWAP5", 0, 1 + 5, false, 3}, // 0x94,
        OpCodeInfo{"SWAP6", 0, 1 + 6, false, 3}, // 0x95,
        OpCodeInfo{"SWAP7", 0, 1 + 7, false, 3}, // 0x96,
        OpCodeInfo{"SWAP8", 0, 1 + 8, false, 3}, // 0x97,
        OpCodeInfo{"SWAP9", 0, 1 + 9, false, 3}, // 0x98,
        OpCodeInfo{"SWAP10", 0, 1 + 10, false, 3}, // 0x99,
        OpCodeInfo{"SWAP11", 0, 1 + 11, false, 3}, // 0x9A,
        OpCodeInfo{"SWAP12", 0, 1 + 12, false, 3}, // 0x9B,
        OpCodeInfo{"SWAP13", 0, 1 + 13, false, 3}, // 0x9C,
        OpCodeInfo{"SWAP14", 0, 1 + 14, false, 3}, // 0x9D,
        OpCodeInfo{"SWAP15", 0, 1 + 15, false, 3}, // 0x9E,
        OpCodeInfo{"SWAP16", 0, 1 + 16, false, 3}, // 0x9F,

        OpCodeInfo{"LOG0", 0, 2 + 0, false, 375}, // 0xA0,
        OpCodeInfo{"LOG1", 0, 2 + 1, false, 750}, // 0xA1,
        OpCodeInfo{"LOG2", 0, 2 + 2, false, 1125}, // 0xA2,
        OpCodeInfo{"LOG3", 0, 2 + 3, false, 1500}, // 0xA3,
        OpCodeInfo{"LOG4", 0, 2 + 4, false, 1875}, // 0xA4,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        unknown_opcode_info, // 0xB0
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        unknown_opcode_info, // 0xC0
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        unknown_opcode_info, // 0xD0
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        unknown_opcode_info, // 0xE0
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,

        OpCodeInfo{"CREATE", 0, 3, true, 32000}, // 0xF0,
        OpCodeInfo{"CALL", 0, 7, true, 100}, // 0xF1,
        OpCodeInfo{"CALLCODE", 0, 7, true, 100}, // 0xF2,
        OpCodeInfo{"RETURN", 0, 2, false, 0}, // 0xF3,
        OpCodeInfo{"DELEGATECALL", 0, 6, true, 100}, // 0xF4,
        OpCodeInfo{"CREATE2", 0, 4, true, 3200}, // 0xF5,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        unknown_opcode_info,
        OpCodeInfo{"STATICCALL", 0, 6, true, 100}, // 0xFA,
        unknown_opcode_info,
        unknown_opcode_info,
        OpCodeInfo{"REVERT", 0, 2, false, 0}, // 0xFD,
        unknown_opcode_info,
        OpCodeInfo{"SELFDESTRUCT", 0, 1, false, 5000} // 0xFF,
    };

    static_assert(
        opcode_info_table.size() == 256,
        "Must have opcode info for exact opcode range [0x00, 0xFF)");

    /**
     * Mnemonic mapping of human-readable opcode names to their underlying byte
     * values.
     */
    enum OpCode : uint8_t
    {
        STOP = 0x00,
        ADD = 0x01,
        MUL = 0x02,
        SUB = 0x03,
        DIV = 0x04,
        SDIV = 0x05,
        MOD = 0x06,
        SMOD = 0x07,
        ADDMOD = 0x08,
        MULMOD = 0x09,
        EXP = 0x0A,
        SIGNEXTEND = 0x0B,
        LT = 0x10,
        GT = 0x11,
        SLT = 0x12,
        SGT = 0x13,
        EQ = 0x14,
        ISZERO = 0x15,
        AND = 0x16,
        OR = 0x17,
        XOR = 0x18,
        NOT = 0x19,
        BYTE = 0x1A,
        SHL = 0x1B,
        SHR = 0x1C,
        SAR = 0x1D,
        SHA3 = 0x20,
        ADDRESS = 0x30,
        BALANCE = 0x31,
        ORIGIN = 0x32,
        CALLER = 0x33,
        CALLVALUE = 0x34,
        CALLDATALOAD = 0x35,
        CALLDATASIZE = 0x36,
        CALLDATACOPY = 0x37,
        CODESIZE = 0x38,
        CODECOPY = 0x39,
        GASPRICE = 0x3A,
        EXTCODESIZE = 0x3B,
        EXTCODECOPY = 0x3C,
        RETURNDATASIZE = 0x3D,
        RETURNDATACOPY = 0x3E,
        EXTCODEHASH = 0x3F,
        BLOCKHASH = 0x40,
        COINBASE = 0x41,
        TIMESTAMP = 0x42,
        NUMBER = 0x43,
        DIFFICULTY = 0x44,
        GASLIMIT = 0x45,
        CHAINID = 0x46,
        SELFBALANCE = 0x47,
        BASEFEE = 0x48,
        BLOBHASH = 0x49,
        BLOBBASEFEE = 0x4A,
        POP = 0x50,
        MLOAD = 0x51,
        MSTORE = 0x52,
        MSTORE8 = 0x53,
        SLOAD = 0x54,
        SSTORE = 0x55,
        JUMP = 0x56,
        JUMPI = 0x57,
        PC = 0x58,
        MSIZE = 0x59,
        GAS = 0x5A,
        JUMPDEST = 0x5B,
        TLOAD = 0x5C,
        TSTORE = 0x5D,
        MCOPY = 0x5E,
        PUSH0 = 0x5F,
        PUSH1 = 0x60,
        PUSH2 = 0x61,
        PUSH3 = 0x62,
        PUSH4 = 0x63,
        PUSH5 = 0x64,
        PUSH6 = 0x65,
        PUSH7 = 0x66,
        PUSH8 = 0x67,
        PUSH9 = 0x68,
        PUSH10 = 0x69,
        PUSH11 = 0x6A,
        PUSH12 = 0x6B,
        PUSH13 = 0x6C,
        PUSH14 = 0x6D,
        PUSH15 = 0x6E,
        PUSH16 = 0x6F,
        PUSH17 = 0x70,
        PUSH18 = 0x71,
        PUSH19 = 0x72,
        PUSH20 = 0x73,
        PUSH21 = 0x74,
        PUSH22 = 0x75,
        PUSH23 = 0x76,
        PUSH24 = 0x77,
        PUSH25 = 0x78,
        PUSH26 = 0x79,
        PUSH27 = 0x7A,
        PUSH28 = 0x7B,
        PUSH29 = 0x7C,
        PUSH30 = 0x7D,
        PUSH31 = 0x7E,
        PUSH32 = 0x7F,
        DUP1 = 0x80,
        DUP2 = 0x81,
        DUP3 = 0x82,
        DUP4 = 0x83,
        DUP5 = 0x84,
        DUP6 = 0x85,
        DUP7 = 0x86,
        DUP8 = 0x87,
        DUP9 = 0x88,
        DUP10 = 0x89,
        DUP11 = 0x8A,
        DUP12 = 0x8B,
        DUP13 = 0x8C,
        DUP14 = 0x8D,
        DUP15 = 0x8E,
        DUP16 = 0x8F,
        SWAP1 = 0x90,
        SWAP2 = 0x91,
        SWAP3 = 0x92,
        SWAP4 = 0x93,
        SWAP5 = 0x94,
        SWAP6 = 0x95,
        SWAP7 = 0x96,
        SWAP8 = 0x97,
        SWAP9 = 0x98,
        SWAP10 = 0x99,
        SWAP11 = 0x9A,
        SWAP12 = 0x9B,
        SWAP13 = 0x9C,
        SWAP14 = 0x9D,
        SWAP15 = 0x9E,
        SWAP16 = 0x9F,
        LOG0 = 0xA0,
        LOG1 = 0xA1,
        LOG2 = 0xA2,
        LOG3 = 0xA3,
        LOG4 = 0xA4,
        CREATE = 0xF0,
        CALL = 0xF1,
        CALLCODE = 0xF2,
        RETURN = 0xF3,
        DELEGATECALL = 0xF4,
        CREATE2 = 0xF5,
        STATICCALL = 0xFA,
        REVERT = 0xFD,
        SELFDESTRUCT = 0xFF
    };

    /**
     * Returns `true` if `opcode` belongs to the `PUSHN` family of EVM opcodes.
     */
    constexpr bool is_push_opcode(uint8_t const opcode)
    {
        return opcode >= PUSH0 && opcode <= PUSH32;
    }

    /**
     * Returns `true` if `opcode` belongs to the `SWAPN` family of EVM opcodes.
     */
    constexpr bool is_swap_opcode(uint8_t const opcode)
    {
        return opcode >= SWAP1 && opcode <= SWAP16;
    }

    /**
     * Returns `true` if `opcode` belongs to the `DUPN` family of EVM opcodes.
     */
    constexpr bool is_dup_opcode(uint8_t const opcode)
    {
        return opcode >= DUP1 && opcode <= DUP16;
    }

    /**
     * Returns `true` if `opcode` is an unknown/invalid EVM opcode.
     */
    constexpr bool is_unknown_opcode(uint8_t const opcode)
    {
        return opcode_info_table[opcode].name == "UNKNOWN";
    }

    /**
     * Returns `true` if `opcode` is a terminator instruction.
     */
    constexpr bool is_terminator_opcode(uint8_t const opcode)
    {
        return opcode == JUMPI || opcode == JUMP || opcode == RETURN ||
               opcode == STOP || opcode == REVERT || opcode == SELFDESTRUCT;
    }

    /**
     * Opcode must be the opcode of some DUPN instruction.
     * Returns `N`.
     */
    constexpr uint8_t get_dup_opcode_index(uint8_t const opcode)
    {
        assert(is_dup_opcode(opcode));
        switch (opcode) {
        case DUP1:
            return 1;
        case DUP2:
            return 2;
        case DUP3:
            return 3;
        case DUP4:
            return 4;
        case DUP5:
            return 5;
        case DUP6:
            return 6;
        case DUP7:
            return 7;
        case DUP8:
            return 8;
        case DUP9:
            return 9;
        case DUP10:
            return 10;
        case DUP11:
            return 11;
        case DUP12:
            return 12;
        case DUP13:
            return 13;
        case DUP14:
            return 14;
        case DUP15:
            return 15;
        case DUP16:
            return 16;
        default:
            std::terminate();
        }
    }

    /**
     * Opcode must be the opcode of some SWAPN instruction.
     * Returns `N`.
     */
    constexpr uint8_t get_swap_opcode_index(uint8_t const opcode)
    {
        assert(is_swap_opcode(opcode));
        switch (opcode) {
        case SWAP1:
            return 1;
        case SWAP2:
            return 2;
        case SWAP3:
            return 3;
        case SWAP4:
            return 4;
        case SWAP5:
            return 5;
        case SWAP6:
            return 6;
        case SWAP7:
            return 7;
        case SWAP8:
            return 8;
        case SWAP9:
            return 9;
        case SWAP10:
            return 10;
        case SWAP11:
            return 11;
        case SWAP12:
            return 12;
        case SWAP13:
            return 13;
        case SWAP14:
            return 14;
        case SWAP15:
            return 15;
        case SWAP16:
            return 16;
        default:
            std::terminate();
        }
    }
}
