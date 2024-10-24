#include "compiler/types.h"
#include "compiler/uint256.h"
#include "intx/intx.hpp"
#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/bytecode.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/opcode_cases.h>
#include <compiler/opcodes.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <utility>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::local_stacks;

    void eval_instruction_fallback(
        bytecode::Instruction const &tok, std::deque<Value> &stack)
    {
        auto const info = opcode_info_table[tok.opcode];
        for (std::size_t i = 0; i < info.min_stack; ++i) {
            stack.pop_front();
        }
        if (info.increases_stack) {
            stack.push_front({ValueIs::COMPUTED, 0});
        }
    }

    void eval_ternary_instruction(
        bytecode::Instruction const &tok, std::deque<Value> &stack,
        std::function<
            uint256_t(uint256_t const &, uint256_t const &, uint256_t const &)>
            f)
    {
        Value const &x = stack[0];
        Value const &y = stack[1];
        Value &z = stack[2];
        if (x.is == ValueIs::LITERAL && y.is == ValueIs::LITERAL &&
            z.is == ValueIs::LITERAL) {
            z.data = f(x.data, y.data, z.data);
            stack.pop_front();
            stack.pop_front();
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_binary_instruction(
        bytecode::Instruction const &tok, std::deque<Value> &stack,
        std::function<uint256_t(uint256_t const &, uint256_t const &)> f)
    {
        Value const &x = stack[0];
        Value &y = stack[1];
        if (x.is == ValueIs::LITERAL && y.is == ValueIs::LITERAL) {
            y.data = f(x.data, y.data);
            stack.pop_front();
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_unary_instruction(
        bytecode::Instruction const &tok, std::deque<Value> &stack,
        std::function<uint256_t(uint256_t const &)> f)
    {
        Value &x = stack[0];
        if (x.is == ValueIs::LITERAL) {
            x.data = f(x.data);
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_instruction(
        bytecode::Instruction const &tok, std::deque<Value> &stack,
        uint64_t codesize)
    {
        switch (tok.opcode) {
        case ADD:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x + y; });
            break;
        case MUL:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x * y; });
            break;
        case SUB:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x - y; });
            break;
        case DIV:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return x / y;
            });
            break;
        case SDIV:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::sdivrem(x, y).quot;
            });
            break;
        case MOD:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return x % y;
            });
            break;
        case SMOD:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::sdivrem(x, y).rem;
            });
            break;
        case ADDMOD:
            eval_ternary_instruction(tok, stack, [](auto &x, auto &y, auto &m) {
                if (m == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::addmod(x, y, m);
            });
            break;
        case MULMOD:
            eval_ternary_instruction(tok, stack, [](auto &x, auto &y, auto &m) {
                if (m == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::mulmod(x, y, m);
            });
            break;
        case EXP:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return intx::exp(x, y); });
            break;
        case SIGNEXTEND:
            eval_binary_instruction(tok, stack, uint256::signextend);
            break;
        case LT:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x < y}; });
            break;
        case GT:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x > y}; });
            break;
        case SLT:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                return uint256_t{intx::slt(x, y)};
            });
            break;
        case SGT:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                return uint256_t{intx::slt(y, x)};
            });
            break;
        case EQ:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x == y}; });
            break;
        case ISZERO:
            eval_unary_instruction(tok, stack, [](auto &x) {
                return uint256_t{x == uint256_t{0}};
            });
            break;
        case AND:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x & y; });
            break;
        case OR:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x | y; });
            break;
        case XOR:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x ^ y; });
            break;
        case NOT:
            eval_unary_instruction(tok, stack, [](auto &x) { return ~x; });
            break;
        case BYTE:
            eval_binary_instruction(tok, stack, uint256::byte);
            break;
        case SHL:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return y << x; });
            break;
        case SHR:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return y >> x; });
            break;
        case SAR:
            eval_binary_instruction(tok, stack, uint256::sar);
            break;
        case CODESIZE:
            stack.emplace_front(ValueIs::LITERAL, uint256_t{codesize});
            break;
        case POP:
            stack.pop_front();
            break;
        case PC:
            stack.emplace_front(ValueIs::LITERAL, tok.offset);
            break;
        case ANY_PUSH:
            stack.emplace_front(ValueIs::LITERAL, tok.data);
            break;
        case ANY_DUP:
            stack.push_front(stack[tok.opcode - DUP1]);
            break;
        case ANY_SWAP:
            std::swap(stack[0], stack[1 + tok.opcode - SWAP1]);
            break;
        default:
            eval_instruction_fallback(tok, stack);
        }
    }
}

namespace monad::compiler::local_stacks
{
    Block LocalStacksIR::to_block(basic_blocks::Block const &&in)
    {
        Block out = {
            0, {}, std::move(in.instrs), in.terminator, in.fallthrough_dest};
        std::deque<Value> stack;

        auto grow_stack_to_min_size = [&](size_t min_size) {
            while (stack.size() < min_size) {
                stack.emplace_back(ValueIs::PARAM_ID, out.min_params);
                out.min_params++;
            }
        };

        for (auto const &tok : out.instrs) {
            auto const opcode = tok.opcode;

            auto const info = opcode_info_table[opcode];

            grow_stack_to_min_size(info.min_stack);

            eval_instruction(tok, stack, codesize);
        }

        switch (out.terminator) {
        case basic_blocks::Terminator::JumpDest:
        case basic_blocks::Terminator::Jump:
        case basic_blocks::Terminator::SelfDestruct:
            grow_stack_to_min_size(1);
            break;
        case basic_blocks::Terminator::JumpI:
        case basic_blocks::Terminator::Return:
        case basic_blocks::Terminator::Revert:
            grow_stack_to_min_size(2);
            break;
        case basic_blocks::Terminator::Stop:
            break;
        default:
            std::terminate(); // unreachable
        }

        out.output.insert(out.output.end(), stack.begin(), stack.end());
        return out;
    }

    LocalStacksIR::LocalStacksIR(basic_blocks::BasicBlocksIR const &&ir)
    {
        codesize = ir.codesize;
        jumpdests = ir.jump_dests();
        blocks = {};

        for (auto const &blk : ir.blocks()) {
            blocks.push_back(to_block(std::move(blk)));
        }
    }

}
