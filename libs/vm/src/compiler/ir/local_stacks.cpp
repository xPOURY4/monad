#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>

#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::local_stacks;
    using namespace monad::utils;

    void
    eval_instruction_fallback(Instruction const &tok, std::deque<Value> &stack)
    {
        for (std::size_t i = 0; i < tok.stack_args(); ++i) {
            stack.pop_front();
        }

        if (tok.increases_stack()) {
            stack.emplace_front(ValueIs::COMPUTED, 0);
        }
    }

    void eval_ternary_instruction(
        Instruction const &tok, std::deque<Value> &stack,
        std::function<
            uint256_t(uint256_t const &, uint256_t const &, uint256_t const &)>
            f)
    {
        Value const &x = stack[0];
        Value const &y = stack[1];
        Value &z = stack[2];
        if (x.is == ValueIs::LITERAL && y.is == ValueIs::LITERAL &&
            z.is == ValueIs::LITERAL) {
            z.literal = f(x.literal, y.literal, z.literal);
            stack.pop_front();
            stack.pop_front();
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_binary_instruction(
        Instruction const &tok, std::deque<Value> &stack,
        std::function<uint256_t(uint256_t const &, uint256_t const &)> f)
    {
        Value const &x = stack[0];
        Value &y = stack[1];
        if (x.is == ValueIs::LITERAL && y.is == ValueIs::LITERAL) {
            y.literal = f(x.literal, y.literal);
            stack.pop_front();
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_unary_instruction(
        Instruction const &tok, std::deque<Value> &stack,
        std::function<uint256_t(uint256_t const &)> f)
    {
        Value &x = stack[0];
        if (x.is == ValueIs::LITERAL) {
            x.literal = f(x.literal);
        }
        else {
            eval_instruction_fallback(tok, stack);
        }
    }

    void eval_instruction(
        Instruction const &tok, std::deque<Value> &stack, uint64_t codesize)
    {
        if (tok.is_push()) {
            stack.emplace_front(ValueIs::LITERAL, tok.immediate_value());
            return;
        }

        if (tok.is_dup()) {
            stack.push_front(stack[tok.index() - 1]);
            return;
        }

        if (tok.is_swap()) {
            std::swap(stack[0], stack[tok.index()]);
            return;
        }

        switch (tok.opcode()) {
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
            eval_binary_instruction(tok, stack, signextend);
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
            eval_binary_instruction(tok, stack, byte);
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
            eval_binary_instruction(tok, stack, sar);
            break;
        case CODESIZE:
            stack.emplace_front(ValueIs::LITERAL, uint256_t{codesize});
            break;
        case POP:
            stack.pop_front();
            break;
        case PC:
            stack.emplace_front(ValueIs::LITERAL, tok.pc());
            break;
        default:
            eval_instruction_fallback(tok, stack);
        }
    }
}

namespace monad::compiler::local_stacks
{
    Value::Value(ValueIs is, uint256_t data)
        : is(is)
    {
        switch (is) {
        case ValueIs::LITERAL:
            literal = data;
            break;
        case ValueIs::PARAM_ID:
            static_assert(sizeof(size_t) <= sizeof(uint64_t));
            MONAD_COMPILER_ASSERT(
                data <= uint256_t{std::numeric_limits<size_t>::max()});
            param = data[0];
        default:
            break;
        }
    };

    Block convert_block(basic_blocks::Block in, uint64_t codesize)
    {
        auto out = Block{
            .min_params = 0,
            .output = {},
            .instrs = std::move(in.instrs),
            .terminator = in.terminator,
            .fallthrough_dest = in.fallthrough_dest,
            .offset = in.offset,
        };

        std::deque<Value> stack;

        auto grow_stack_to_min_size = [&](size_t min_size) {
            while (stack.size() < min_size) {
                stack.emplace_back(ValueIs::PARAM_ID, out.min_params);
                out.min_params++;
            }
        };

        for (auto const &instr : out.instrs) {
            grow_stack_to_min_size(instr.stack_args());
            eval_instruction(instr, stack, codesize);
        }

        grow_stack_to_min_size(basic_blocks::terminator_inputs(out.terminator));

        out.output.insert(out.output.end(), stack.begin(), stack.end());
        return out;
    }

}
