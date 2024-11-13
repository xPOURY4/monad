#include <compiler/ir/basic_blocks.h>
#include <compiler/ir/instruction.h>
#include <compiler/ir/local_stacks.h>
#include <compiler/opcodes.h>
#include <compiler/types.h>
#include <compiler/uint256.h>
#include <utils/assert.h>

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

    void eval_instruction_fallback(
        basic_blocks::Instruction const &tok, std::deque<Value> &stack)
    {
        auto const &info = tok.info();
        for (std::size_t i = 0; i < info.min_stack; ++i) {
            stack.pop_front();
        }
        if (info.increases_stack) {
            stack.emplace_front(ValueIs::COMPUTED, 0);
        }
    }

    void eval_ternary_instruction(
        basic_blocks::Instruction const &tok, std::deque<Value> &stack,
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
        basic_blocks::Instruction const &tok, std::deque<Value> &stack,
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
        basic_blocks::Instruction const &tok, std::deque<Value> &stack,
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
        basic_blocks::Instruction const &tok, std::deque<Value> &stack,
        uint64_t codesize)
    {
        using enum basic_blocks::InstructionCode;
        switch (tok.code) {
        case Add:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x + y; });
            break;
        case Mul:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x * y; });
            break;
        case Sub:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x - y; });
            break;
        case Div:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return x / y;
            });
            break;
        case SDiv:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::sdivrem(x, y).quot;
            });
            break;
        case Mod:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return x % y;
            });
            break;
        case SMod:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                if (y == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::sdivrem(x, y).rem;
            });
            break;
        case AddMod:
            eval_ternary_instruction(tok, stack, [](auto &x, auto &y, auto &m) {
                if (m == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::addmod(x, y, m);
            });
            break;
        case MulMod:
            eval_ternary_instruction(tok, stack, [](auto &x, auto &y, auto &m) {
                if (m == uint256_t{0}) {
                    return uint256_t{0};
                }
                return intx::mulmod(x, y, m);
            });
            break;
        case Exp:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return intx::exp(x, y); });
            break;
        case SignExtend:
            eval_binary_instruction(tok, stack, uint256::signextend);
            break;
        case Lt:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x < y}; });
            break;
        case Gt:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x > y}; });
            break;
        case SLt:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                return uint256_t{intx::slt(x, y)};
            });
            break;
        case SGt:
            eval_binary_instruction(tok, stack, [](auto &x, auto &y) {
                return uint256_t{intx::slt(y, x)};
            });
            break;
        case Eq:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return uint256_t{x == y}; });
            break;
        case IsZero:
            eval_unary_instruction(tok, stack, [](auto &x) {
                return uint256_t{x == uint256_t{0}};
            });
            break;
        case And:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x & y; });
            break;
        case Or:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x | y; });
            break;
        case XOr:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return x ^ y; });
            break;
        case Not:
            eval_unary_instruction(tok, stack, [](auto &x) { return ~x; });
            break;
        case Byte:
            eval_binary_instruction(tok, stack, uint256::byte);
            break;
        case Shl:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return y << x; });
            break;
        case Shr:
            eval_binary_instruction(
                tok, stack, [](auto &x, auto &y) { return y >> x; });
            break;
        case Sar:
            eval_binary_instruction(tok, stack, uint256::sar);
            break;
        case CodeSize:
            stack.emplace_front(ValueIs::LITERAL, uint256_t{codesize});
            break;
        case Pop:
            stack.pop_front();
            break;
        case Pc:
            stack.emplace_front(ValueIs::LITERAL, tok.offset);
            break;
        case Push:
            stack.emplace_front(ValueIs::LITERAL, tok.operand);
            break;
        case Dup:
            stack.push_front(stack[tok.index - 1]);
            break;
        case Swap:
            std::swap(stack[0], stack[tok.index]);
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

        for (auto const &tok : out.instrs) {
            auto const &info = tok.info();

            grow_stack_to_min_size(info.min_stack);

            eval_instruction(tok, stack, codesize);
        }

        grow_stack_to_min_size(basic_blocks::terminator_inputs(out.terminator));

        out.output.insert(out.output.end(), stack.begin(), stack.end());
        return out;
    }

    LocalStacksIR::LocalStacksIR(basic_blocks::BasicBlocksIR ir)
        : jumpdests(std::move(ir.jump_dests()))
        , codesize(ir.codesize)
    {
        for (auto &blk : ir.blocks()) {
            blocks.push_back(convert_block(std::move(blk), codesize));
        }
    }

}
