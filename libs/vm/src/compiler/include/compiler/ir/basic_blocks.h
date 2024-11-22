#pragma once

#include <compiler/ir/instruction.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>
#include <utils/assert.h>

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
     * Opcode info for a given terminator.
     *
     * Note that info for `FallThrough` and `InvalidInstruction` is
     * `unknown_opcode_info`, and that the information here is with respect to
     * the latest stable EVM revision.
     */
    constexpr OpCodeInfo terminator_info(Terminator t)
    {
        auto table = opcode_table<EVMC_LATEST_STABLE_REVISION>();

        using enum Terminator;
        switch (t) {
        case JumpI:
            return table[JUMPI];
        case Return:
            return table[RETURN];
        case Revert:
            return table[REVERT];
        case Jump:
            return table[JUMP];
        case SelfDestruct:
            return table[SELFDESTRUCT];
        case Stop:
            return table[STOP];
        case FallThrough:
        case InvalidInstruction:
            return unknown_opcode_info;
        default:
            std::unreachable();
        }
    }

    /**
     * Return the number of input stack elements consumed by each block
     * terminator.
     */
    constexpr std::size_t terminator_inputs(Terminator t)
    {
        return terminator_info(t).min_stack;
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

    /**
     * In this representation, the underlying EVM code has been grouped into
     * basic blocks by splitting the program at terminator points.
     *
     * Blocks are assigned integer identifiers based on the order in which they
     * appear in the original program, and a table of jump destinations is
     * constructed that maps byte offsets in the original program onto these
     * block identifiers.
     */
    template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
    class BasicBlocksIR
    {
    public:
        /**
         * Construct basic blocks from a bytecode program.
         */
        BasicBlocksIR(std::span<std::uint8_t const>);
        BasicBlocksIR(std::initializer_list<std::uint8_t>);

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
        static Instruction scan_from(
            std::span<std::uint8_t const> bytes, std::uint32_t &current_offset);

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

    template <evmc_revision Rev>
    Instruction BasicBlocksIR<Rev>::scan_from(
        std::span<std::uint8_t const> bytes, std::uint32_t &current_offset)
    {
        auto opcode = bytes[current_offset];
        auto info = opcode_table<Rev>()[opcode];

        auto imm_size = info.num_args;
        auto opcode_offset = current_offset;

        current_offset++;

        if (info == unknown_opcode_info) {
            return Instruction::invalid(opcode_offset, opcode);
        }

        auto imm_value = utils::from_bytes(
            imm_size, bytes.size() - current_offset, &bytes[current_offset]);
        current_offset += imm_size;

        return Instruction(opcode_offset, opcode, imm_value, info);
    }

    template <evmc_revision Rev>
    BasicBlocksIR<Rev>::BasicBlocksIR(std::span<std::uint8_t const> bytes)
        : codesize(bytes.size())
    {
        enum class St
        {
            INSIDE_BLOCK,
            OUTSIDE_BLOCK
        };

        St st = St::INSIDE_BLOCK;

        add_block(0);

        auto current_offset = std::uint32_t{0};
        auto first = true;

        while (current_offset < bytes.size()) {
            auto inst = scan_from(bytes, current_offset);

            if (first && inst.opcode() == JUMPDEST) {
                add_jump_dest();
                first = false;
                continue;
            }

            if (st == St::OUTSIDE_BLOCK) {
                if (inst.opcode() == JUMPDEST) {
                    add_block(inst.pc());
                    st = St::INSIDE_BLOCK;
                    add_jump_dest();
                }
            }
            else {
                MONAD_COMPILER_ASSERT(st == St::INSIDE_BLOCK);
                switch (inst.opcode()) {
                case JUMPDEST:
                    add_fallthrough_terminator(Terminator::FallThrough);
                    add_block(inst.pc());
                    add_jump_dest();
                    break;

                case JUMPI:
                    add_fallthrough_terminator(Terminator::JumpI);
                    add_block(inst.pc() + 1);
                    // a corner case where we fall through JUMPI
                    // into a block starting with JUMPDEST, in which case we
                    // don't want to immediately FallThrough again, but
                    // instead just advance tok and mark the block as being
                    // a jumpdest
                    if (current_offset < bytes.size()) {
                        auto next_offset = current_offset;
                        if (scan_from(bytes, next_offset).opcode() ==
                            JUMPDEST) {
                            current_offset += 1;
                            add_jump_dest();
                        }
                    }
                    break;

                case JUMP:
                    add_terminator(Terminator::Jump);
                    st = St::OUTSIDE_BLOCK;
                    break;

                case RETURN:
                    add_terminator(Terminator::Return);
                    st = St::OUTSIDE_BLOCK;
                    break;

                case STOP:
                    add_terminator(Terminator::Stop);
                    st = St::OUTSIDE_BLOCK;
                    break;

                case REVERT:
                    add_terminator(Terminator::Revert);
                    st = St::OUTSIDE_BLOCK;
                    break;

                case SELFDESTRUCT:
                    add_terminator(Terminator::SelfDestruct);
                    st = St::OUTSIDE_BLOCK;
                    break;

                default:
                    // invalid or instruction opcode
                    if (inst.is_valid()) {
                        blocks_.back().instrs.push_back(inst);
                    }
                    else {
                        add_terminator(Terminator::InvalidInstruction);
                        st = St::OUTSIDE_BLOCK;
                    }
                    break;
                }
            }

            first = false;
        }
    }

    template <evmc_revision Rev>
    BasicBlocksIR<Rev>::BasicBlocksIR(std::initializer_list<std::uint8_t> bytes)
        : BasicBlocksIR(std::span(bytes))
    {
    }

    /*
     * IR
     */

    template <evmc_revision Rev>
    std::vector<Block> const &BasicBlocksIR<Rev>::blocks() const
    {
        return blocks_;
    }

    template <evmc_revision Rev>
    std::vector<Block> &BasicBlocksIR<Rev>::blocks()
    {
        return blocks_;
    }

    template <evmc_revision Rev>
    Block const &BasicBlocksIR<Rev>::block(block_id id) const
    {
        return blocks_.at(id);
    }

    template <evmc_revision Rev>
    std::unordered_map<byte_offset, block_id> const &
    BasicBlocksIR<Rev>::jump_dests() const
    {
        return jump_dests_;
    }

    template <evmc_revision Rev>
    std::unordered_map<byte_offset, block_id> &BasicBlocksIR<Rev>::jump_dests()
    {
        return jump_dests_;
    }

    template <evmc_revision Rev>
    bool BasicBlocksIR<Rev>::is_valid() const
    {
        auto all_blocks_valid =
            std::all_of(blocks_.begin(), blocks_.end(), [](auto const &b) {
                return b.is_valid();
            });

        auto all_dests_valid = std::all_of(
            jump_dests_.begin(), jump_dests_.end(), [this](auto const &entry) {
                auto [offset, block_id] = entry;
                return block_id < blocks_.size();
            });

        return all_blocks_valid && all_dests_valid;
    }

    /*
     * IR: Private construction methods
     */

    template <evmc_revision Rev>
    block_id BasicBlocksIR<Rev>::curr_block_id() const
    {
        return blocks_.size() - 1;
    }

    template <evmc_revision Rev>
    byte_offset BasicBlocksIR<Rev>::curr_block_offset() const
    {
        return blocks_.back().offset;
    }

    template <evmc_revision Rev>
    void BasicBlocksIR<Rev>::add_jump_dest()
    {
        assert(blocks_.back().instrs.empty());
        jump_dests_.emplace(curr_block_offset(), curr_block_id());
    }

    template <evmc_revision Rev>
    void BasicBlocksIR<Rev>::add_block(byte_offset offset)
    {
        blocks_.push_back(Block{.offset = offset});
    }

    template <evmc_revision Rev>
    void BasicBlocksIR<Rev>::add_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
    }

    template <evmc_revision Rev>
    void BasicBlocksIR<Rev>::add_fallthrough_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
        blocks_.back().fallthrough_dest = curr_block_id() + 1;
    }
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

template <evmc_revision Rev>
struct std::formatter<monad::compiler::basic_blocks::BasicBlocksIR<Rev>>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    auto format(
        monad::compiler::basic_blocks::BasicBlocksIR<Rev> const &ir,
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
