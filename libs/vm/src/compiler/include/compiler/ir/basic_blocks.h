#pragma once

#include <compiler/evm_opcodes.h>
#include <compiler/ir/instruction.h>
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

    struct JumpDest
    {
        std::uint32_t pc;
    };

    template <evmc_revision Rev>
    constexpr Terminator evm_op_to_terminator(std::uint8_t evm_op)
    {
        using enum Terminator;

        if (is_unknown_opcode<Rev>(evm_op)) {
            return InvalidInstruction;
        }

        switch (evm_op) {
        case JUMPI:
            return JumpI;
        case JUMP:
            return Jump;
        case RETURN:
            return Return;
        case STOP:
            return Stop;
        case REVERT:
            return Revert;
        case SELFDESTRUCT:
            return SelfDestruct;
        default:
            MONAD_COMPILER_ASSERT(false);
        }
    }

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

    /**
     * Return true if this terminator can implicitly fall through to the next
     * block in sequence.
     */
    constexpr bool is_fallthrough_terminator(Terminator t)
    {
        return t == Terminator::FallThrough || t == Terminator::JumpI;
    }

    /**
     * Gas usage for a given terminator.
     *
     * Returns a pair (minimum_static_gas, uses_dynamic_gas).
     */
    constexpr std::pair<std::uint16_t, bool> terminator_gas_info(Terminator t)
    {
        using enum Terminator;
        switch (t) {
        case JumpI:
            return {10, false};
        case Return:
            return {0, false};
        case Revert:
            return {0, true};
        case Jump:
            return {8, false};
        case SelfDestruct:
            return {5000, true};
        case Stop:
            return {0, false};
        case FallThrough:
        case InvalidInstruction:
            return {0, false};
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
         * The basic block byte code offset.
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

    template <evmc_revision Rev>
    struct RevisionMarker
    {
    };

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
        template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
        BasicBlocksIR(std::span<std::uint8_t const>, RevisionMarker<Rev> = {});

        template <evmc_revision Rev = EVMC_LATEST_STABLE_REVISION>
        BasicBlocksIR(
            std::initializer_list<std::uint8_t>, RevisionMarker<Rev> = {});

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
        template <evmc_revision Rev>
        static std::variant<Instruction, Terminator, JumpDest> scan_from(
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

    template <evmc_revision Rev, typename... Args>
    BasicBlocksIR make_ir(Args &&...);

    template <evmc_revision Rev>
    std::variant<Instruction, Terminator, JumpDest> BasicBlocksIR::scan_from(
        std::span<std::uint8_t const> bytes, std::uint32_t &current_offset)
    {
        auto opcode = bytes[current_offset];
        auto opcode_offset = current_offset;

        auto info = opcode_table<Rev>()[opcode];
        current_offset++;

        if (is_control_flow_opcode<Rev>(opcode)) {
            return evm_op_to_terminator<Rev>(opcode);
        }

        if (opcode == JUMPDEST) {
            return JumpDest{opcode_offset};
        }

        MONAD_COMPILER_ASSERT(info != unknown_opcode_info);

        auto imm_size = info.num_args;
        auto imm_value = utils::from_bytes(
            imm_size, bytes.size() - current_offset, &bytes[current_offset]);

        current_offset += imm_size;

        return Instruction(
            opcode_offset,
            evm_op_to_opcode(opcode),
            imm_value,
            info.min_gas,
            info.min_stack,
            info.index,
            info.increases_stack,
            info.dynamic_gas);
    }

    template <evmc_revision Rev>
    BasicBlocksIR::BasicBlocksIR(
        std::span<std::uint8_t const> bytes, RevisionMarker<Rev>)
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
            auto inst = scan_from<Rev>(bytes, current_offset);

            if (first && std::holds_alternative<JumpDest>(inst)) {
                add_jump_dest();
                first = false;
                continue;
            }

            if (st == St::OUTSIDE_BLOCK) {
                if (std::holds_alternative<JumpDest>(inst)) {
                    add_block(std::get<JumpDest>(inst).pc);
                    st = St::INSIDE_BLOCK;
                    add_jump_dest();
                }
            }
            else {
                MONAD_COMPILER_ASSERT(st == St::INSIDE_BLOCK);

                auto handle_terminator = [&](Terminator t) {
                    using enum Terminator;
                    MONAD_COMPILER_ASSERT(t != FallThrough);

                    if (t == JumpI) {
                        add_fallthrough_terminator(JumpI);
                        add_block(current_offset);
                        // a corner case where we fall through JUMPI
                        // into a block starting with JUMPDEST, in which
                        // case we don't want to immediately FallThrough
                        // again, but instead just advance tok and mark
                        // the block as being a jumpdest
                        if (current_offset < bytes.size()) {
                            auto next_offset = current_offset;
                            auto next_inst = scan_from<Rev>(bytes, next_offset);
                            if (std::holds_alternative<JumpDest>(next_inst)) {
                                current_offset += 1;
                                add_jump_dest();
                            }
                        }
                        return;
                    }

                    add_terminator(t);
                    st = St::OUTSIDE_BLOCK;
                };

                std::visit(
                    Cases{
                        handle_terminator,

                        [&](Instruction i) {
                            blocks_.back().instrs.push_back(i);
                        },

                        [&](JumpDest jd) {
                            add_fallthrough_terminator(Terminator::FallThrough);
                            add_block(jd.pc);
                            add_jump_dest();
                        },
                    },
                    inst);
            }
            first = false;
        }
    }

    template <evmc_revision Rev>
    BasicBlocksIR::BasicBlocksIR(
        std::initializer_list<std::uint8_t> bytes, RevisionMarker<Rev> rm)
        : BasicBlocksIR(std::span(bytes), rm)
    {
    }

    template <evmc_revision Rev, typename... Args>
    BasicBlocksIR make_ir(Args &&...args)
    {
        return BasicBlocksIR(
            std::forward<Args>(args)..., RevisionMarker<Rev>{});
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
