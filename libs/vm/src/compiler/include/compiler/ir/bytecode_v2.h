#pragma once

#include <compiler/opcodes.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace monad::compiler
{
    class Instruction
    {
    public:
        constexpr Instruction(
            std::uint32_t pc, std::uint8_t opcode, OpCodeInfo info) noexcept;

        constexpr Instruction(
            std::uint32_t pc, std::uint8_t opcode,
            utils::uint256_t immediate_value, OpCodeInfo info) noexcept;

        static constexpr Instruction
        invalid(std::uint32_t pc, std::uint8_t opcode) noexcept;

        constexpr bool is_valid() const noexcept;
        constexpr bool is_dup() const noexcept;
        constexpr bool is_swap() const noexcept;
        constexpr bool is_push() const noexcept;
        constexpr bool is_log() const noexcept;

        constexpr utils::uint256_t const &immediate_value() const noexcept;
        constexpr std::uint32_t pc() const noexcept;
        constexpr std::uint16_t static_gas_cost() const noexcept;
        constexpr std::uint8_t opcode() const noexcept;
        constexpr std::uint8_t stack_args() const noexcept;
        constexpr std::uint8_t index() const noexcept;
        constexpr bool increases_stack() const noexcept;
        constexpr bool dynamic_gas() const noexcept;

    private:
        constexpr Instruction(std::uint32_t pc, std::uint8_t opcode) noexcept;

        utils::uint256_t immediate_value_;
        std::uint32_t pc_;
        std::uint16_t static_gas_cost_;
        std::uint8_t opcode_;
        std::uint8_t stack_args_;
        std::uint8_t index_;
        bool is_valid_;
        bool increases_stack_;
        bool dynamic_gas_;
    };

    constexpr Instruction::Instruction(
        std::uint32_t pc, std::uint8_t opcode, OpCodeInfo info) noexcept
        : immediate_value_(0)
        , pc_(pc)
        , static_gas_cost_(static_cast<std::uint16_t>(info.min_gas))
        , opcode_(opcode)
        , stack_args_(static_cast<std::uint8_t>(info.min_stack))
        , index_(get_opcode_index(opcode))
        , is_valid_(true)
        , increases_stack_(info.increases_stack)
        , dynamic_gas_(false) // TODO
    {
        MONAD_COMPILER_ASSERT(!is_push_opcode(opcode));
    }

    constexpr Instruction::Instruction(
        std::uint32_t pc, std::uint8_t opcode, utils::uint256_t immediate_value,
        OpCodeInfo info) noexcept
        : immediate_value_(immediate_value)
        , pc_(pc)
        , static_gas_cost_(static_cast<std::uint16_t>(info.min_gas))
        , opcode_(opcode)
        , stack_args_(static_cast<std::uint8_t>(info.min_stack))
        , index_(get_opcode_index(opcode))
        , is_valid_(true)
        , increases_stack_(info.increases_stack)
        , dynamic_gas_(false) // TODO
    {
        MONAD_COMPILER_ASSERT(is_push_opcode(opcode));
    }

    constexpr Instruction::Instruction(
        std::uint32_t pc, std::uint8_t opcode) noexcept
        : immediate_value_(0)
        , pc_(pc)
        , static_gas_cost_(0)
        , opcode_(opcode)
        , stack_args_(0)
        , index_(0)
        , is_valid_(false)
        , increases_stack_(false)
        , dynamic_gas_(false)
    {
    }

    constexpr Instruction
    Instruction::invalid(std::uint32_t pc, std::uint8_t opcode) noexcept
    {
        return Instruction(pc, opcode);
    }

    constexpr bool Instruction::is_valid() const noexcept
    {
        return is_valid_;
    }

    constexpr bool Instruction::is_dup() const noexcept
    {
        return is_valid() && is_dup_opcode(opcode_);
    }

    constexpr bool Instruction::is_swap() const noexcept
    {
        return is_valid() && is_swap_opcode(opcode_);
    }

    constexpr bool Instruction::is_push() const noexcept
    {
        return is_valid() && is_push_opcode(opcode_);
    }

    constexpr bool Instruction::is_log() const noexcept
    {
        return is_valid() && is_log_opcode(opcode_);
    }

    constexpr utils::uint256_t const &
    Instruction::immediate_value() const noexcept
    {
        MONAD_COMPILER_ASSERT(is_push());
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

    constexpr std::uint8_t Instruction::opcode() const noexcept
    {
        return opcode_;
    }

    constexpr std::uint8_t Instruction::stack_args() const noexcept
    {
        return stack_args_;
    }

    constexpr std::uint8_t Instruction::index() const noexcept
    {
        MONAD_COMPILER_ASSERT(is_push() || is_dup() || is_swap() || is_log());
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
}
