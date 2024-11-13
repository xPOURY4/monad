#pragma once

#include <compiler/ir/bytecode.h>
#include <compiler/ir/instruction.h>
#include <compiler/types.h>

#include <limits>
#include <unordered_map>
#include <utility>

namespace monad::compiler::basic_blocks
{
    /**
     * Represents the subset of EVM instructions that may terminate a basic
     * block.
     *
     * After executing one of these instructions, control may not transfer
     * linearly to the next instruction in the program. Instead, execution may
     * either jump to a new program counter, or terminate entirely by handing
     * control back to the VM host.
     *
     * Note that `FallThrough` does not directly correspond to an EVM opcode;
     * these terminators occur when a block ends with a `JUMPDEST` instruction.
     * Here, the `JUMPDEST` needs to occur at the beginning of the following
     * block for gas accounting, but the current block still needs to be
     * terminated.
     */
    enum class Terminator
    {
        FallThrough,
        JumpI,
        Jump,
        Return,
        Stop,
        Revert,
        SelfDestruct,
        InvalidInstruction,
    };

    /**
     * Return true if this terminator can implicitly fall through to the next
     * block in sequence.
     */
    constexpr bool is_fallthrough_terminator(Terminator t)
    {
        return t == Terminator::FallThrough || t == Terminator::JumpI;
    }

    /**
     * Return the number of input stack elements consumed by each block
     * terminator.
     */
    constexpr std::size_t terminator_inputs(Terminator t)
    {
        using enum Terminator;
        switch (t) {
        case JumpI:
        case Return:
        case Revert:
            return 2;
        case Jump:
        case SelfDestruct:
            return 1;
        case Stop:
        case FallThrough:
        case InvalidInstruction:
            return 0;
        default:
            std::unreachable();
        }
    }

    /**
     * A basic block is a linear sequence of EVM instructions ending with a
     * single terminator.
     */
    struct Block
    {
        /**
         * The linear sequence of instructions that make up this block.
         *
         * It is legal for the body of a block to be empty; every valid block is
         * terminated.
         */
        std::vector<Instruction> instrs = {};

        /**
         * The terminator that ends this block.
         */
        Terminator terminator = Terminator::Stop;

        /**
         * The block ID that control should fall through to at the end of this
         * block, if the terminator of the block is a `JUMPI` instruction or an
         * implicit fallthrough.
         */
        block_id fallthrough_dest = INVALID_BLOCK_ID;

        /**
         * Does the original bytecode block start with a JUMPDEST opcode
         * This is needed for gas accounting
         */
        byte_offset offset = 0;

        /**
         * Returns true if this block is well-formed.
         *
         * A well-formed block satisfies the following conditions:
         * - None of the instructions in `instrs` is a terminator.
         * - The `fallthrough_dest` is valid iff the block is terminated by a
         *   `JUMPI` instruction or an implicit fallthrough.
         */
        bool is_valid() const;
    };

    bool operator==(Block const &a, Block const &b);

    std::optional<Instruction>
    to_instruction(monad::compiler::bytecode::Instruction const &i);

    /**
     * In this representation, the underlying EVM code has been grouped into
     * basic blocks by splitting the program at terminator points.
     *
     * Blocks are assigned integer identifiers based on the order in which they
     * appear in the original program, and a table of jump destinations is
     * constructed that maps byte offsets in the original program onto these
     * block identifiers.
     */
    class BasicBlocksIR
    {
    public:
        /**
         * Construct basic blocks from a bytecode program.
         */
        BasicBlocksIR(bytecode::BytecodeIR const &byte_code);

        /**
         * The basic blocks in the program.
         *
         * Blocks have an implicit integer identifier based on the order in
         * which they appear in this vector.
         */
        std::vector<Block> const &blocks() const;
        std::vector<Block> &blocks();

        /// Size of bytecode
        uint64_t codesize;

        /**
         * Retrieve a block by its identifier.
         */
        Block const &block(block_id id) const;

        /**
         * A table mapping byte offsets into the original EVM code onto block
         * identifiers.
         */
        std::unordered_map<byte_offset, block_id> const &jump_dests() const;
        std::unordered_map<byte_offset, block_id> &jump_dests();

        /**
         * A program in this representation is valid if:
         * - Each block in the program is valid.
         * - Each entry in the jumpdest table maps to a valid block.
         */
        bool is_valid() const;

    private:
        std::vector<Block> blocks_;
        std::unordered_map<byte_offset, block_id> jump_dests_;

        /**
         * During construction, the ID of the block currently being built.
         */
        block_id curr_block_id() const;

        /**
         * During construction, the byte offset of the block currently being
         * built.
         */
        byte_offset curr_block_offset() const;

        /**
         * During construction, add a new entry to the jump destination table
         * when a `JUMPDEST` instruction is parsed.
         */
        void add_jump_dest();

        /**
         * During construction, begin building a new block.
         */
        void add_block(byte_offset offset);

        /**
         * During construction, set the terminator for the block currently being
         * built.
         */
        void add_terminator(Terminator t);

        /**
         * During construction, set the terminator for the block currently being
         * built, and set the fallthrough destination ID to that of the next
         * block that will be built.
         */
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
        auto v = [t] {
            using enum monad::compiler::basic_blocks::Terminator;

            switch (t) {
            case FallThrough:
                return "FallThrough";
            case JumpI:
                return "JumpI";
            case Jump:
                return "Jump";
            case Return:
                return "Return";
            case Revert:
                return "Revert";
            case SelfDestruct:
                return "SelfDestruct";
            case Stop:
                return "Stop";
            case InvalidInstruction:
                return "InvalidInstruction";
            }

            std::unreachable();
        }();

        return std::format_to(ctx.out(), "{}", v);
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
        for (auto const &blk : ir.blocks()) {
            std::format_to(ctx.out(), "  block {} - 0x{}:\n", i, blk.offset);
            std::format_to(ctx.out(), "{}", blk);
            i++;
        }
        std::format_to(ctx.out(), "\n  jumpdests:\n");
        for (auto const &[k, v] : ir.jump_dests()) {
            std::format_to(ctx.out(), "    {}:{}\n", k, v);
        }
        return std::format_to(ctx.out(), "");
    }
};
