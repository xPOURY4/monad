#pragma once

#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <array>
#include <cstdint>
#include <format>
#include <string_view>
#include <vector>

namespace monad::compiler::bytecode
{
    /**
     * Represents an instruction comprising an EVM opcode together with an
     * associated immediate value (where appropriate) and metadata.
     */
    struct Instruction
    {
        /**
         * The offset into the source program that this token was found
         * originally.
         */
        byte_offset offset;

        /**
         * Raw byte value representing the EVM opcode of this instruction; this
         * value is not validated and may correspond to an invalid instruction.
         */
        uint8_t opcode;

        /**
         * The 256-bit immediate value associated with an instruction.
         *
         * Used only when this instruction has an opcode in the `PUSHN` family,
         * and zero otherwise.
         */
        uint256_t data;
    };

    bool operator==(Instruction const &a, Instruction const &b);

    /**
     * Represents an EVM program where raw program bytes have been resolved into
     * a sequence of logical instructions.
     *
     * This representation is conceptually very close to the original binary
     * format of an EVM program. The only changes made to produce it are:
     *
     * - Parsing and grouping of immediate values following PUSH instructions.
     * - Padding zero bytes at the end of a program that is too short.
     */
    class BytecodeIR
    {
    public:
        /**
         * Construct a bytecode program from a vector of raw bytes.
         *
         * No validation or analysis is performed beyond grouping immediate
         * values for `PUSH` instructions; invalid input bytes will produce an
         * invalid output program.
         */
        BytecodeIR(std::vector<uint8_t> const &byte_code);

        /**
         * The logical EVM instructions lexed from the original binary.
         */
        std::vector<Instruction> instructions;
    };

}

/*
 * Formatter Implementations
 */

template <>
struct std::formatter<monad::compiler::bytecode::Instruction>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::bytecode::Instruction const &tok,
        std::format_context &ctx) const
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
struct std::formatter<monad::compiler::bytecode::BytecodeIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::bytecode::BytecodeIR const &ir,
        std::format_context &ctx) const
    {
        std::format_to(ctx.out(), "bytecode:");
        for (auto const &tok : ir.instructions) {
            std::format_to(ctx.out(), "\n  {}", tok);
        }
        return std::format_to(ctx.out(), "\n");
    }
};
