#pragma once

#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace monad::compiler
{

    struct Token
    {
        byte_offset offset;
        uint8_t opcode;
        uint256_t data; // only used by push
    };

    bool operator==(Token const &a, Token const &b);

    class BytecodeIR
    {
    public:
        BytecodeIR(std::vector<uint8_t> const &byte_code);
        std::vector<Token> tokens;
    };

}

template <>
struct std::formatter<monad::compiler::Token>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto
    format(monad::compiler::Token const &tok, std::format_context &ctx) const
    {
        return std::format_to(
            ctx.out(),
            "({}, {}, {})",
            tok.offset,
            monad::compiler::opcode_info_table[tok.opcode].name,
            tok.data);
    }
};

template <>
struct std::formatter<monad::compiler::BytecodeIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::BytecodeIR const &ir, std::format_context &ctx) const
    {
        std::format_to(ctx.out(), "bytecode:");
        for (monad::compiler::Token const &tok : ir.tokens) {
            std::format_to(ctx.out(), "\n  {}", tok);
        }
        return std::format_to(ctx.out(), "\n");
    }
};
