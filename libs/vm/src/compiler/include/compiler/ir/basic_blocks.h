#pragma once

#include <compiler/ir/bytecode.h>
#include <compiler/types.h>

#include <limits>
#include <unordered_map>
#include <utility>

namespace monad::compiler::basic_blocks
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

    struct Block
    {
        std::vector<bytecode::Instruction> instrs;
        Terminator terminator;

        // value for JumpI and JumpDest, otherwise
        // INVALID_BLOCK_ID
        block_id fallthrough_dest = INVALID_BLOCK_ID;
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

/*
 * Formatter Implementations
 */

template <>
struct std::formatter<monad::compiler::basic_blocks::Terminator>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::basic_blocks::Terminator const &t,
        std::format_context &ctx) const
    {
#define CASE(t)                                                                \
    case monad::compiler::basic_blocks::Terminator::t: {                       \
        return #t;                                                             \
    }

        auto v = [&t] {
            switch (t) {
                CASE(JumpDest);
                CASE(JumpI);
                CASE(Jump);
                CASE(Return);
                CASE(Revert);
                CASE(SelfDestruct);
                CASE(Stop);
                default: std::unreachable();
            }
        }();

        return std::format_to(ctx.out(), "{}", v);

#undef CASE
    }
};

template <>
struct std::formatter<monad::compiler::basic_blocks::Block>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::basic_blocks::Block const &blk,
        std::format_context &ctx) const
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
struct std::formatter<monad::compiler::basic_blocks::BasicBlocksIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::basic_blocks::BasicBlocksIR const &ir,
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
