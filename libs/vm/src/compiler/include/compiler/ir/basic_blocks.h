#pragma once

#include <compiler/ir/bytecode.h>

#include <limits>
#include <unordered_map>

using block_id = std::size_t;

namespace monad::compiler
{

    enum class Terminator
    {
        JumpDest,
        JumpI,
        Jump,
        Return,
        Stop,
        Revert,
        SelfDestruct
    };

    inline constexpr block_id INVALID_BLOCK_ID =
        std::numeric_limits<block_id>::max();

    struct Block
    {
        std::vector<bytecode::Instruction> instrs;
        Terminator terminator;
        block_id fallthrough_dest; // value for JumpI and JumpDest, otherwise
                                   // INVALID_BLOCK_ID
    };

    bool operator==(Block const &a, Block const &b);

    class BasicBlocksIR
    {
    public:
        BasicBlocksIR(bytecode::BytecodeIR const &byte_code);
        std::unordered_map<byte_offset, block_id> jumpdests;
        std::vector<Block> blocks;

    private:
        block_id curr_block_id() const;
        void add_jump_dest(byte_offset offset);
        void add_block();
        void add_terminator(Terminator t);
        void add_fallthrough_terminator(Terminator t);
    };
}

template <>
struct std::formatter<monad::compiler::Terminator>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::compiler::Terminator const &t, std::format_context &ctx) const
    {
        std::string_view v;
        switch (t) {
        case monad::compiler::Terminator::JumpDest:
            v = "JumpDest";
            break;
        case monad::compiler::Terminator::JumpI:
            v = "JumpI";
            break;
        case monad::compiler::Terminator::Jump:
            v = "Jump";
            break;
        case monad::compiler::Terminator::Return:
            v = "Return";
            break;
        case monad::compiler::Terminator::Revert:
            v = "Revert";
            break;
        case monad::compiler::Terminator::SelfDestruct:
            v = "SelfDestruct";
            break;
        default:
            assert(t == monad::compiler::Terminator::Stop);
            v = "Stop";
            break;
        }
        return std::format_to(ctx.out(), "{}", v);
    }
};

template <>
struct std::formatter<monad::compiler::Block>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::compiler::Block const &blk, std::format_context &ctx) const
    {

        for (auto const &tok : blk.instrs) {
            std::format_to(ctx.out(), "      {}\n", tok);
        }

        std::format_to(ctx.out(), "    {}", blk.terminator);
        if (blk.fallthrough_dest != monad::compiler::INVALID_BLOCK_ID) {
            std::format_to(ctx.out(), " {}", blk.fallthrough_dest);
        }
        return std::format_to(ctx.out(), "\n");
    }
};

template <>
struct std::formatter<monad::compiler::BasicBlocksIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::BasicBlocksIR const &ir,
        std::format_context &ctx) const
    {

        std::format_to(ctx.out(), "basic_blocks:\n");
        int i = 0;
        for (auto const &blk : ir.blocks) {
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
