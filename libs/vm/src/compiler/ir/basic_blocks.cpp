#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/instruction.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace monad::compiler::basic_blocks
{
    /*
     * IR
     */

    BasicBlocksIR::BasicBlocksIR(bytecode::BytecodeIR const &byte_code)
    {
        enum class St
        {
            INSIDE_BLOCK,
            OUTSIDE_BLOCK
        };

        St st = St::INSIDE_BLOCK;

        codesize = byte_code.codesize;

        add_block(0);

        auto tok = byte_code.instructions.begin();
        if (tok != byte_code.instructions.end() && tok->opcode == JUMPDEST) {
            add_jump_dest();
            ++tok;
        }

        for (; tok != byte_code.instructions.end(); ++tok) {
            if (st == St::OUTSIDE_BLOCK) {
                if (tok->opcode == JUMPDEST) {
                    add_block(tok->offset);
                    st = St::INSIDE_BLOCK;
                    add_jump_dest();
                }
            }
            else {
                assert(st == St::INSIDE_BLOCK);
                switch (tok->opcode) {
                case JUMPDEST:
                    add_fallthrough_terminator(Terminator::FallThrough);
                    add_block(tok->offset);
                    add_jump_dest();
                    break;

                case JUMPI:
                    add_fallthrough_terminator(Terminator::JumpI);
                    add_block(tok->offset + 1);
                    // a corner case where we fall through JUMPI
                    // into a block starting with JUMPDEST, in which case we
                    // don't want to immediately FallThrough again, but
                    // instead just advance tok and mark the block as being
                    // a jumpdest
                    if (tok + 1 != byte_code.instructions.end() &&
                        (tok + 1)->opcode == JUMPDEST) {
                        ++tok;
                        add_jump_dest();
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

                default: // invalid or instruction opcode
                    if (auto instr = to_instruction(*tok)) {
                        blocks_.back().instrs.push_back(std::move(*instr));
                    }
                    else {
                        add_terminator(Terminator::InvalidInstruction);
                        st = St::OUTSIDE_BLOCK;
                    }
                    break;
                }
            }
        }
    }

    std::vector<Block> const &BasicBlocksIR::blocks() const
    {
        return blocks_;
    }

    std::vector<Block> &BasicBlocksIR::blocks()
    {
        return blocks_;
    }

    Block const &BasicBlocksIR::block(block_id id) const
    {
        return blocks_.at(id);
    }

    std::unordered_map<byte_offset, block_id> const &
    BasicBlocksIR::jump_dests() const
    {
        return jump_dests_;
    }

    std::unordered_map<byte_offset, block_id> &BasicBlocksIR::jump_dests()
    {
        return jump_dests_;
    }

    bool BasicBlocksIR::is_valid() const
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

    block_id BasicBlocksIR::curr_block_id() const
    {
        return blocks_.size() - 1;
    }

    byte_offset BasicBlocksIR::curr_block_offset() const
    {
        return blocks_.back().offset;
    }

    void BasicBlocksIR::add_jump_dest()
    {
        assert(blocks_.back().instrs.empty());
        jump_dests_.emplace(curr_block_offset(), curr_block_id());
    }

    void BasicBlocksIR::add_block(byte_offset offset)
    {
        blocks_.push_back(Block{.offset = offset});
    }

    void BasicBlocksIR::add_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
    }

    void BasicBlocksIR::add_fallthrough_terminator(Terminator t)
    {
        blocks_.back().terminator = t;
        blocks_.back().fallthrough_dest = curr_block_id() + 1;
    }

    std::optional<Instruction>
    to_instruction(monad::compiler::bytecode::Instruction const &i)
    {
        if (is_push_opcode(i.opcode)) {
            return std::optional<Instruction>{
                {i.offset,
                 InstructionCode::Push,
                 static_cast<uint8_t>(i.opcode - PUSH0),
                 i.data}};
        }
        if (is_dup_opcode(i.opcode)) {
            return std::optional<Instruction>{
                {i.offset,
                 InstructionCode::Dup,
                 static_cast<uint8_t>(i.opcode - DUP1 + 1),
                 0}};
        }
        if (is_swap_opcode(i.opcode)) {
            return std::optional<Instruction>{
                {i.offset,
                 InstructionCode::Swap,
                 static_cast<uint8_t>(i.opcode - SWAP1 + 1),
                 0}};
        }
        if (is_log_opcode(i.opcode)) {
            return std::optional<Instruction>{
                {i.offset,
                 InstructionCode::Log,
                 static_cast<uint8_t>(i.opcode - LOG0),
                 0}};
        }
        if (is_control_flow_opcode(i.opcode)) {
            return std::nullopt;
        }
        return std::optional<Instruction>{
            {i.offset, static_cast<InstructionCode>(i.opcode), 0, 0}};
    }

    std::optional<Instruction>
    to_instruction(monad::compiler::Instruction const &i)
    {
        if (i.is_push()) {
            return Instruction{
                i.pc(), InstructionCode::Push, i.index(), i.immediate_value()};
        }

        if (i.is_dup()) {
            return Instruction{i.pc(), InstructionCode::Dup, i.index(), 0};
        }

        if (i.is_swap()) {
            return Instruction{i.pc(), InstructionCode::Swap, i.index(), 0};
        }

        if (i.is_log()) {
            return Instruction{i.pc(), InstructionCode::Log, i.index(), 0};
        }

        if (i.is_control_flow()) {
            return std::nullopt;
        }

        return Instruction{
            i.pc(), static_cast<InstructionCode>(i.opcode()), 0, 0};
    }

    /*
     * Block
     */

    bool Block::is_valid() const
    {
        return is_fallthrough_terminator(terminator) ==
               (fallthrough_dest != INVALID_BLOCK_ID);
    }

    bool operator==(Block const &a, Block const &b)
    {
        return std::tie(a.instrs, a.terminator, a.fallthrough_dest, a.offset) ==
               std::tie(b.instrs, b.terminator, b.fallthrough_dest, b.offset);
    }
}
