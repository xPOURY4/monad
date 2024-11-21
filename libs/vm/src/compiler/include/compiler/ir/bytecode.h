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

        template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
        static constexpr Instruction
        lookup(std::uint32_t pc, std::uint8_t opcode) noexcept;

        template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
        static constexpr Instruction lookup(
            std::uint32_t pc, std::uint8_t opcode,
            utils::uint256_t immediate_value) noexcept;

        constexpr bool is_valid() const noexcept;
        constexpr bool is_dup() const noexcept;
        constexpr bool is_swap() const noexcept;
        constexpr bool is_push() const noexcept;
        constexpr bool is_log() const noexcept;
        constexpr bool is_control_flow() const noexcept;

        constexpr utils::uint256_t const &immediate_value() const noexcept;
        constexpr std::uint32_t pc() const noexcept;
        constexpr std::uint16_t static_gas_cost() const noexcept;
        constexpr std::uint8_t opcode() const noexcept;
        constexpr std::uint8_t stack_args() const noexcept;
        constexpr std::uint8_t index() const noexcept;
        constexpr bool increases_stack() const noexcept;
        constexpr bool dynamic_gas() const noexcept;

        friend constexpr bool
        operator==(Instruction const &, Instruction const &) noexcept;

    private:
        constexpr Instruction(std::uint32_t pc, std::uint8_t opcode) noexcept;

        constexpr auto as_tuple() const noexcept;

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

    template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
    class Bytecode
    {
    public:
        static constexpr evmc_revision revision = Rev;

        Bytecode(std::span<std::uint8_t const>);
        Bytecode(std::initializer_list<std::uint8_t>);

        std::vector<Instruction> const &instructions() const noexcept;
        std::size_t code_size() const noexcept;

    private:
        std::vector<Instruction> instructions_;
        std::size_t code_size_;
    };

    /*
     * Bytecode
     */

    template <evmc_revision Rev>
    Bytecode<Rev>::Bytecode(std::span<std::uint8_t const> bytes)
        : instructions_{}
        , code_size_{bytes.size()}
    {
        auto current_offset = std::uint32_t{0};

        while (current_offset < bytes.size()) {
            auto opcode = bytes[current_offset];
            auto info = opcode_table<Rev>()[opcode];

            auto imm_size = info.num_args;
            auto opcode_offset = current_offset;

            current_offset++;

            if (info == unknown_opcode_info) {
                instructions_.push_back(
                    Instruction::invalid(opcode_offset, opcode));
            }
            else {
                auto imm_value = utils::from_bytes(
                    imm_size,
                    bytes.size() - current_offset,
                    &bytes[current_offset]);

                instructions_.emplace_back(
                    opcode_offset, opcode, imm_value, info);

                current_offset += imm_size;
            }
        }
    }

    template <evmc_revision Rev>
    Bytecode<Rev>::Bytecode(std::initializer_list<std::uint8_t> bytes)
        : Bytecode(std::span{bytes})
    {
    }

    template <evmc_revision Rev>
    std::vector<Instruction> const &Bytecode<Rev>::instructions() const noexcept
    {
        return instructions_;
    }

    template <evmc_revision Rev>
    std::size_t Bytecode<Rev>::code_size() const noexcept
    {
        return code_size_;
    }

    /*
     * Instruction
     */

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
        , dynamic_gas_(info.dynamic_gas)
    {
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
        , dynamic_gas_(info.dynamic_gas)
    {
        MONAD_COMPILER_ASSERT(immediate_value == 0 || is_push());
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
        MONAD_COMPILER_ASSERT(!is_push());
    }

    constexpr Instruction
    Instruction::invalid(std::uint32_t pc, std::uint8_t opcode) noexcept
    {
        return Instruction(pc, opcode);
    }

    template <evmc_revision Rev>
    constexpr Instruction
    Instruction::lookup(std::uint32_t pc, std::uint8_t opcode) noexcept
    {
        auto info = opcode_table<Rev>()[opcode];
        return Instruction(pc, opcode, info);
    }

    template <evmc_revision Rev>
    constexpr Instruction Instruction::lookup(
        std::uint32_t pc, std::uint8_t opcode,
        utils::uint256_t immediate_value) noexcept
    {
        auto info = opcode_table<Rev>()[opcode];
        return Instruction(pc, opcode, immediate_value, info);
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

    constexpr bool Instruction::is_control_flow() const noexcept
    {
        return is_valid() && is_control_flow_opcode(opcode_);
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
        if (!inst.is_valid()) {
            return std::format_to(ctx.out(), "INVALID");
        }

        // It's ok to use the latest revision here because an instruction that's
        // too new should already have been flagged as invalid.
        auto info = monad::compiler::opcode_table<
            EVMC_LATEST_STABLE_REVISION>()[inst.opcode()];

        if (inst.is_push() && inst.index() > 0) {
            return std::format_to(
                ctx.out(), "{} {}", info.name, inst.immediate_value());
        }

        return std::format_to(ctx.out(), "{}", info.name);
    }
};

template <evmc_revision Rev>
struct std::formatter<monad::compiler::Bytecode<Rev>>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::Bytecode<Rev> const &ir,
        std::format_context &ctx) const
    {
        auto sep = "";
        for (auto const &inst : ir.instructions()) {
            std::format_to(ctx.out(), "{}{}", sep, inst);
            sep = "\n";
        }

        return ctx.out();
    }
};
