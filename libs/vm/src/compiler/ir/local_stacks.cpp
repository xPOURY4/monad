#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/opcodes.h>

#include <cstddef>
#include <deque>
#include <utility>
#include <vector>

namespace monad::compiler::local_stacks
{
    Block LocalStacksIR::to_block(monad::compiler::Block const &&in)
    {
        Block out = {
            0, {}, std::move(in.instrs), in.terminator, in.fallthrough_dest};
        std::deque<Value> stack;

        for (auto const &tok : out.instrs) {
            auto const opcode = tok.opcode;

            if (is_push_opcode(opcode)) {
                stack.emplace_front(ValueIs::LITERAL, tok.data);
                continue;
            }

            if (opcode == PC) {
                stack.emplace_front(ValueIs::LITERAL, tok.offset);
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

            for (std::size_t i = 0; i < info.min_stack; ++i) {
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

    LocalStacksIR::LocalStacksIR(BasicBlocksIR const &&ir)
    {
        jumpdests = ir.jumpdests;
        blocks = {};

        for (monad::compiler::Block const &blk : ir.blocks) {
            blocks.push_back(to_block(std::move(blk)));
        }
    }

}
