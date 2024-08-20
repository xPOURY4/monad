#include "compiler/ir/bytecode.h"
#include <compiler/ir/instruction.h>
#include <compiler/ir/registers.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace monad::compiler::registers
{
    bool RegistersIR::is_push_opcode(uint8_t const opcode)
    {
        return opcode >= PUSH0 && opcode <= PUSH32;
    }

    bool RegistersIR::is_swap_opcode(uint8_t const opcode)
    {
        return opcode >= SWAP1 && opcode <= SWAP16;
    }

    bool RegistersIR::is_dup_opcode(uint8_t const opcode)
    {
        return opcode >= DUP1 && opcode <= DUP16;
    }

    Block RegistersIR::to_block(monad::compiler::Block const &in)
    {
        Block out = {0, {}, in.terminator, in.fallthrough_dest};
        std::deque<Value> stack;

        for (auto const &tok : in.instrs) {
            auto const opcode = tok.token_opcode;

            if (is_push_opcode(opcode)) {
                stack.emplace_front(ValueIs::LITERAL, tok.token_data);
                continue;
            }

            if (opcode == PC) {
                stack.emplace_front(ValueIs::LITERAL, tok.token_offset);
                continue;
            }

            auto const info = opcode_info_table[opcode];
            // grow input stack as necessary to ensure enough values for the
            // given opcode
            while (stack.size() < info.min_stack) {
                stack.emplace_back(ValueIs::PARAM_ID, out.min_params);
                out.min_params++;
            }

            if (opcode == POP) {
                stack.pop_front();
                continue;
            }

            if (is_dup_opcode(opcode)) {
                stack.push_front(stack[opcode - DUP1]);
                continue;
            }

            if (is_swap_opcode(opcode)) {
                auto tmp = stack[0];
                uint const i = 1 + opcode - SWAP1;
                stack[0] = stack[i];
                stack[i] = tmp;
                continue;
            }

            std::vector<Value> args;
            for (std::size_t i = 0; i < info.min_stack; ++i) {
                args.push_back(stack[0]);
                stack.pop_front();
            }

        if (info.increases_stack) {
            stack.push_front({ValueIs::COMPUTED, 0});
            continue;
        }

    }

        for (auto const &val : stack) {
            out.output.push_back(val);
        }

        return out;
    }

    RegistersIR::RegistersIR(InstructionIR const &ir)
    {
        jumpdests = ir.jumpdests;
        blocks = {};

        for (monad::compiler::Block const &blk : ir.blocks) {
            blocks.push_back(to_block(blk));
        }
    }

}
