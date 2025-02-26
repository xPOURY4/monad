#pragma once

#include <monad/evm/opcodes.hpp>

#include <array>

namespace monad::fuzzing
{
    using enum monad::compiler::EvmOpCode;

    constexpr auto safe_non_terminators = std::array{
        ADD,
        MUL,
        SUB,
        DIV,
        SDIV,
        MOD,
        SMOD,
        ADDMOD,
        MULMOD,
        EXP,
        SIGNEXTEND,
        LT,
        GT,
        SLT,
        SGT,
        EQ,
        ISZERO,
        AND,
        OR,
        XOR,
        NOT,
        BYTE,
        SHL,
        SHR,
        SAR,
        SHA3,
        ADDRESS,
        BALANCE,
        ORIGIN,
        CALLER,
        CALLVALUE,
        CALLDATALOAD,
        CALLDATASIZE,
        CALLDATACOPY,
        CODESIZE,
        CODECOPY,
        GASPRICE,
        EXTCODESIZE,
        EXTCODECOPY,
        RETURNDATASIZE,
        RETURNDATACOPY,
        EXTCODEHASH,
        BLOCKHASH,
        COINBASE,
        TIMESTAMP,
        NUMBER,
        DIFFICULTY,
        GASLIMIT,
        CHAINID,
        SELFBALANCE,
        BASEFEE,
        BLOBHASH,
        BLOBBASEFEE,
        POP,
        MLOAD,
        MSTORE,
        MSTORE8,
        SLOAD,
        SSTORE,
        PC,
        MSIZE,
        GAS,
        TLOAD,
        TSTORE,
        MCOPY,
        DUP1,
        DUP2,
        DUP3,
        DUP4,
        DUP5,
        DUP6,
        DUP7,
        DUP8,
        DUP9,
        DUP10,
        DUP11,
        DUP12,
        DUP13,
        DUP14,
        DUP15,
        DUP16,
        SWAP1,
        SWAP2,
        SWAP3,
        SWAP4,
        SWAP5,
        SWAP6,
        SWAP7,
        SWAP8,
        SWAP9,
        SWAP10,
        SWAP11,
        SWAP12,
        SWAP13,
        SWAP14,
        SWAP15,
        SWAP16,
        LOG0,
        LOG1,
        LOG2,
        LOG3,
        LOG4,
        CREATE,
        CREATE2,
    };

    constexpr auto terminators = std::array{
        STOP,
        REVERT,
        RETURN,
        JUMPDEST,
        JUMPI,
        JUMP,
        SELFDESTRUCT,
    };

    constexpr auto exit_terminators = std::array{
        STOP,
        REVERT,
        RETURN,
        SELFDESTRUCT,
    };

    constexpr bool is_exit_terminator(std::uint8_t opcode) noexcept
    {
        return std::find(
                   exit_terminators.begin(), exit_terminators.end(), opcode) !=
               exit_terminators.end();
    }

    static_assert(is_exit_terminator(STOP));

    constexpr auto jump_terminators = std::array{
        JUMP,
        JUMPI,
        JUMPDEST,
    };

    std::vector<std::uint8_t> const &
    memory_operands(std::uint8_t const opcode) noexcept;

    bool uses_memory(std::uint8_t const opcode) noexcept;
}
