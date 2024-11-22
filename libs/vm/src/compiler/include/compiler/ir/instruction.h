#pragma once

#include <compiler/opcodes.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <vector>

namespace monad::compiler
{
    enum class OpCode : std::uint8_t
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
        Origin = 0x32,
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

    constexpr OpCode evm_op_to_opcode(std::uint8_t op)
    {
        using enum OpCode;

        if (is_push_opcode(op)) {
            return Push;
        }

        if (is_swap_opcode(op)) {
            return Swap;
        }

        if (is_dup_opcode(op)) {
            return Dup;
        }

        if (is_log_opcode(op)) {
            return Log;
        }

        return OpCode(op);
    }

    class Instruction
    {
    public:
        constexpr Instruction(
            std::uint32_t pc, std::uint8_t opcode, OpCodeInfo info) noexcept;

        constexpr Instruction(
            std::uint32_t pc, std::uint8_t opcode,
            utils::uint256_t immediate_value, OpCodeInfo info) noexcept;

        constexpr utils::uint256_t const &immediate_value() const noexcept;
        constexpr std::uint32_t pc() const noexcept;
        constexpr std::uint16_t static_gas_cost() const noexcept;
        constexpr OpCode opcode() const noexcept;
        constexpr std::uint8_t stack_args() const noexcept;
        constexpr std::uint8_t index() const noexcept;
        constexpr bool increases_stack() const noexcept;
        constexpr bool dynamic_gas() const noexcept;

        friend constexpr bool
        operator==(Instruction const &, Instruction const &) noexcept;

    private:
        constexpr auto as_tuple() const noexcept;

        utils::uint256_t immediate_value_;
        std::uint32_t pc_;
        std::uint16_t static_gas_cost_;
        OpCode opcode_;
        std::uint8_t stack_args_;
        std::uint8_t index_;
        bool is_valid_;
        bool increases_stack_;
        bool dynamic_gas_;
    };

    /*
     * Instruction
     */

    constexpr Instruction::Instruction(
        std::uint32_t pc, std::uint8_t evm_opcode,
        utils::uint256_t immediate_value, OpCodeInfo info) noexcept
        : immediate_value_(immediate_value)
        , pc_(pc)
        , static_gas_cost_(static_cast<std::uint16_t>(info.min_gas))
        , opcode_(evm_op_to_opcode(evm_opcode))
        , stack_args_(static_cast<std::uint8_t>(info.min_stack))
        , index_(get_opcode_index(evm_opcode))
        , is_valid_(true)
        , increases_stack_(info.increases_stack)
        , dynamic_gas_(info.dynamic_gas)
    {
        MONAD_COMPILER_ASSERT(immediate_value == 0 || opcode() == OpCode::Push);
    }

    constexpr Instruction::Instruction(
        std::uint32_t pc, std::uint8_t opcode, OpCodeInfo info) noexcept
        : Instruction(pc, opcode, 0, info)
    {
    }

    constexpr utils::uint256_t const &
    Instruction::immediate_value() const noexcept
    {
        MONAD_COMPILER_ASSERT(opcode() == OpCode::Push);
        return immediate_value_;
    }

    constexpr std::uint32_t Instruction::pc() const noexcept
    {
        return pc_;
    }

    constexpr std::uint16_t Instruction::static_gas_cost() const noexcept
    {
        return static_gas_cost_;
    }

    constexpr OpCode Instruction::opcode() const noexcept
    {
        return opcode_;
    }

    constexpr std::uint8_t Instruction::stack_args() const noexcept
    {
        return stack_args_;
    }

    constexpr std::uint8_t Instruction::index() const noexcept
    {
        MONAD_COMPILER_ASSERT(
            opcode() == OpCode::Push || opcode() == OpCode::Swap ||
            opcode() == OpCode::Dup || opcode() == OpCode::Log);
        return index_;
    }

    constexpr bool Instruction::increases_stack() const noexcept
    {
        return increases_stack_;
    }

    constexpr bool Instruction::dynamic_gas() const noexcept
    {
        return dynamic_gas_;
    }

    constexpr auto Instruction::as_tuple() const noexcept
    {
        return std::tie(
            immediate_value_,
            pc_,
            static_gas_cost_,
            opcode_,
            stack_args_,
            index_,
            is_valid_,
            increases_stack_,
            dynamic_gas_);
    }

    constexpr bool
    operator==(Instruction const &a, Instruction const &b) noexcept
    {
        return a.as_tuple() == b.as_tuple();
    }
}

/*
 * Formatter Implementations
 */

template <>
struct std::formatter<monad::compiler::Instruction>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::Instruction const &inst,
        std::format_context &ctx) const
    {
        using enum monad::compiler::OpCode;

        auto base_op = std::to_underlying(inst.opcode());

        switch (inst.opcode()) {
        case Dup:
        case Swap:
            base_op += static_cast<std::uint8_t>(inst.index() - 1u);
            break;
        case Push:
        case Log:
            base_op += inst.index();
            break;
        default:
            break;
        }

        // It's ok to use the latest revision here because an instruction that's
        // too new should already have been flagged as invalid.
        auto info = monad::compiler::opcode_table<
            EVMC_LATEST_STABLE_REVISION>()[base_op];

        if (inst.opcode() == Push && inst.index() > 0) {
            return std::format_to(
                ctx.out(), "{} {}", info.name, inst.immediate_value());
        }

        return std::format_to(ctx.out(), "{}", info.name);
    }
};
