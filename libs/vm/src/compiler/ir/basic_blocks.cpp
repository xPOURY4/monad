#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <cassert>
#include <tuple>
#include <vector>

namespace monad::compiler::basic_blocks
{

    bool operator==(Block const &a, Block const &b)
    {
        return std::tie(a.instrs, a.terminator, a.fallthrough_dest) ==
               std::tie(b.instrs, b.terminator, b.fallthrough_dest);
    }

    block_id BasicBlocksIR::curr_block_id() const
    {
        return blocks_.size() - 1;
    }

    void BasicBlocksIR::add_jump_dest(byte_offset offset)
    {
        jumpdests.emplace(offset, curr_block_id());
    }

    void BasicBlocksIR::add_block()
    {
        blocks_.emplace_back(
            std::vector<bytecode::Instruction>{}, Terminator::Stop);
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

    std::vector<Block> const& BasicBlocksIR::blocks() const
    {
        return blocks_;
    }

    BasicBlocksIR::BasicBlocksIR(bytecode::BytecodeIR const &byte_code)
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

}
