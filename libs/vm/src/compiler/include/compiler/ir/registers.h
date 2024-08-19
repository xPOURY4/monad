#pragma once

#include <compiler/ir/bytecode.h>
#include <compiler/ir/instruction.h>

using RegisterId = uint256_t;

namespace monad::compiler::registers
{
    inline constexpr RegisterId NO_REGISTER_ID =
        std::numeric_limits<RegisterId>::max();

    enum class ValueIs
    {
        PARAM_ID,
        REGISTER_ID,
        LITERAL
    };

    struct Value
    {
        ValueIs is;
        uint256_t data;
    };

    struct Instr
    {
        RegisterId result; // INVALID_REGISTER_ID if the instruction doesn't
                           // return a value
        Token instr;
        std::vector<Value> params;
    };

    struct Block
    {
        std::size_t min_params;
        std::vector<Instr> instrs;
        std::vector<Value> output;

        Terminator terminator;
        block_id fallthrough_dest; // value for JumpI and JumpDest, otherwise
                                   // INVALID_BLOCK_ID
    };

    class RegistersIR
    {
    public:
        RegistersIR(InstructionIR const &ir);
        std::unordered_map<byte_offset, block_id> jumpdests;
        std::vector<Block> blocks;

    private:
        bool is_push_opcode(uint8_t const opcode);
        bool is_dup_opcode(uint8_t const opcode);
        bool is_swap_opcode(uint8_t const opcode);

        Block to_block(monad::compiler::Block const &block);
    };

}

template <>
struct std::formatter<monad::compiler::registers::Value>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::registers::Value const &val,
        std::format_context &ctx) const
    {
        switch (val.is) {
        case monad::compiler::registers::ValueIs::PARAM_ID:
            return std::format_to(
                ctx.out(), "%p{}", intx::to_string(val.data, 10));
        case monad::compiler::registers::ValueIs::REGISTER_ID:
            return std::format_to(
                ctx.out(), "%r{}", intx::to_string(val.data, 10));
        default:
            return std::format_to(ctx.out(), "{}", val.data);
        }
    }
};

template <>
struct std::formatter<monad::compiler::registers::Instr>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::registers::Instr const &instr,
        std::format_context &ctx) const
    {
        if (instr.result != monad::compiler::registers::NO_REGISTER_ID) {
            std::format_to(
                ctx.out(), "%r{} = ", intx::to_string(instr.result, 10));
        }

        std::format_to(
            ctx.out(),
            "{} [",
            monad::compiler::opcode_info_table[instr.instr.token_opcode].name);
        for (monad::compiler::registers::Value const &val : instr.params) {
            std::format_to(ctx.out(), " {}", val);
        }
        return std::format_to(ctx.out(), " ]");
    }
};

template <>
struct std::formatter<monad::compiler::registers::Block>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::registers::Block const &blk,
        std::format_context &ctx) const
    {

        std::format_to(ctx.out(), "    min_params: {}\n", blk.min_params);

        for (monad::compiler::registers::Instr const &instr : blk.instrs) {
            std::format_to(ctx.out(), "      {}\n", instr);
        }

        std::format_to(ctx.out(), "    {}", blk.terminator);
        if (blk.fallthrough_dest != monad::compiler::INVALID_BLOCK_ID) {
            std::format_to(ctx.out(), " {}", blk.fallthrough_dest);
        }
        std::format_to(ctx.out(), "\n    output: [");
        for (monad::compiler::registers::Value const &val : blk.output) {
            std::format_to(ctx.out(), " {}", val);
        }
        return std::format_to(ctx.out(), " ]\n");
    }
};

template <>
struct std::formatter<monad::compiler::registers::RegistersIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::registers::RegistersIR const &ir,
        std::format_context &ctx) const
    {

        std::format_to(ctx.out(), "registers:\n");
        int i = 0;
        for (monad::compiler::registers::Block const &blk : ir.blocks) {
            std::format_to(ctx.out(), "  block {}:\n", i);
            std::format_to(ctx.out(), "{}", blk);
            i++;
        }
        std::format_to(ctx.out(), "\n  jumpdests:\n");
        for (auto const &[k, v] : ir.jumpdests) {
            std::format_to(ctx.out(), "    {}:{}\n", k, v);
        }
        return std::format_to(ctx.out(), "");
    }
};
