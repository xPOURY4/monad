#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <cassert>
#include <tuple>
#include <vector>

namespace monad::compiler::basic_blocks
{
    /*
     * IR
     */

    BasicBlocksIR::BasicBlocksIR(bytecode::BytecodeIR const &byte_code)
        : blocks_{}
        , jump_dests_{}
    {
        enum class St
        {
            INSIDE_BLOCK,
            OUTSIDE_BLOCK
        };

        St st = St::INSIDE_BLOCK;

        add_block();

        for (auto const &tok : byte_code.instructions) {
            if (st == St::OUTSIDE_BLOCK) {
                if (tok.opcode == JUMPDEST) {
                    add_block();
                    st = St::INSIDE_BLOCK;
                    add_jump_dest(tok.offset);
                }
            }
            else {
                assert(st == St::INSIDE_BLOCK);
                switch (tok.opcode) {
                case JUMPDEST:
                    if (blocks_.back().instrs.size() >
                        0) { // jumpdest terminator
                        add_fallthrough_terminator(Terminator::JumpDest);
                        add_block();
                    }
                    add_jump_dest(tok.offset);
                    break;

                case JUMPI:
                    add_fallthrough_terminator(Terminator::JumpI);
                    add_block();
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

                default: // instruction opcode
                    blocks_.back().instrs.push_back(tok);
                    break;
                }
            }
        }
    }

    std::vector<Block> const &BasicBlocksIR::blocks() const
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

    void BasicBlocksIR::add_jump_dest(byte_offset offset)
    {
        jump_dests_.emplace(offset, curr_block_id());
    }

    void BasicBlocksIR::add_block()
    {
        blocks_.emplace_back();
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

    /*
     * Block
     */

    bool Block::is_valid() const
    {
        auto no_terminators =
            std::none_of(instrs.begin(), instrs.end(), [](auto const &i) {
                return is_terminator_opcode(i.opcode);
            });

        auto fallthrough_iff = is_fallthrough_terminator(terminator) ==
                               (fallthrough_dest != INVALID_BLOCK_ID);

        return no_terminators && fallthrough_iff;
    }

    bool operator==(Block const &a, Block const &b)
    {
        return std::tie(a.instrs, a.terminator, a.fallthrough_dest) ==
               std::tie(b.instrs, b.terminator, b.fallthrough_dest);
    }
}
