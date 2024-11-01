#pragma once

#include <compiler/ir/bytecode.h>

namespace monad::compiler::basic_blocks
{
    enum class InstructionCode
    {
        Add = 0x01,
        Mul = 0x02,
        Sub = 0x03,
        Div = 0x04,
        SDiv = 0x05,
        Mod = 0x06,
        SMod = 0x07,
        AddMod = 0x08,
        MulMod = 0x09,
        Exp = 0x0A,
        SignExtend = 0x0B,
        Lt = 0x10,
        Gt = 0x11,
        SLt = 0x12,
        SGt = 0x13,
        Eq = 0x14,
        IsZero = 0x15,
        And = 0x16,
        Or = 0x17,
        XOr = 0x18,
        Not = 0x19,
        Byte = 0x1A,
        Shl = 0x1B,
        Shr = 0x1C,
        Sar = 0x1D,
        Sha3 = 0x20,
        Address = 0x30,
        Balance = 0x31,
        Orign = 0x32,
        Caller = 0x33,
        CallValue = 0x34,
        CallDataLoad = 0x35,
        CallDataSize = 0x36,
        CallDataCopy = 0x37,
        CodeSize = 0x38,
        CodeCopy = 0x39,
        GasPrice = 0x3A,
        ExtCodeSize = 0x3B,
        ExtCodeCopy = 0x3C,
        ReturnDataSize = 0x3D,
        ReturnDataCopy = 0x3E,
        ExtCodeHash = 0x3F,
        BlockHash = 0x40,
        Coinbase = 0x41,
        Timestamp = 0x42,
        Number = 0x43,
        Difficulty = 0x44,
        GasLimit = 0x45,
        ChainId = 0x46,
        SelfBalance = 0x47,
        BaseFee = 0x48,
        BlobHash = 0x49,
        BlobBaseFee = 0x4A,
        Pop = 0x50,
        MLoad = 0x51,
        MStore = 0x52,
        MStore8 = 0x53,
        SLoad = 0x54,
        SStore = 0x55,
        Pc = 0x58,
        MSize = 0x59,
        Gas = 0x5A,
        TLoad = 0x5C,
        TStore = 0x5D,
        MCopy = 0x5E,
        Push = 0x5F,
        Dup = 0x80,
        Swap = 0x90,
        Log = 0xA0,
        Create = 0xF0,
        Call = 0xF1,
        CallCode = 0xF2,
        DelegateCall = 0xF4,
        Create2 = 0xF5,
        StaticCall = 0xFA,
    };

    /**
     * Represents an higher level instruction parsed from an EVM opcode,
     * where instructions such as DUP*,PUSH*,etc have been merged into a
     * single instruction with an index.
     * For PUSH* instructions, the operand holds the constant value.
     * This type also does not hold control flow nor invalid instructions,
     * as those are encoded as the Terminator enum of a block.
     */
    struct Instruction
    {
        /**
         * The offset into the source program that this token was found
         * originally.
         */
        byte_offset offset;

        InstructionCode code;
        uint8_t index; // for DUPn, SWAPn, PUSHn, LOGn
        uint256_t operand = 0; // for PUSH

        OpCodeInfo const &info() const;
    };

    bool operator==(Instruction const &a, Instruction const &b);
}

/*
 * Formatter Implementations
 */

template <>
struct std::formatter<monad::compiler::basic_blocks::Instruction>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::basic_blocks::Instruction const &tok,
        std::format_context &ctx) const
    {
        return std::format_to(
            ctx.out(),
            "({}, {}, {})",
            tok.offset,
            tok.info().name,
            tok.operand);
    }
};
