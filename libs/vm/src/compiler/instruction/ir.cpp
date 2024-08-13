#include "ir.h"
#include "../bytecode/ir.h"

#include <cassert>
#include <vector>

block_id InstructionIR::curr_block_id()
{
    return blocks.size() - 1;
}

void InstructionIR::add_jump_dest(byte_offset offset)
{
    jumpdests.emplace(offset, curr_block_id());
}

void InstructionIR::add_block()
{
    blocks.emplace_back(
        std::vector<Token>{}, Terminator::Stop, INVALID_BLOCK_ID);
}

void InstructionIR::add_terminator(Terminator t)
{
    blocks.back().terminator = t;
}

void InstructionIR::add_fallthrough_terminator(Terminator t)
{
    blocks.back().terminator = t;
    blocks.back().fallthrough_dest = curr_block_id() + 1;
}

InstructionIR::InstructionIR(BytecodeIR &byte_code)
{

    enum class St
    {
        INSIDE_BLOCK,
        OUTSIDE_BLOCK
    };

    St st = St::INSIDE_BLOCK;

    add_block();

    for (auto &tok : byte_code.tokens) {
        if (st == St::OUTSIDE_BLOCK) {
            if (tok.token_opcode == JUMPDEST) {
                add_block();
                st = St::INSIDE_BLOCK;
                add_jump_dest(tok.token_offset);
            }
        }
        else {
            assert(st == St::INSIDE_BLOCK);
            switch (tok.token_opcode) {
            case JUMPDEST:
                if (blocks.back().instrs.size() > 0) { // jumpdest terminator
                    add_fallthrough_terminator(Terminator::JumpDest);
                    add_block();
                }
                add_jump_dest(tok.token_offset);
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
                blocks.back().instrs.push_back(tok);
                break;
            }
        }
    }
}
