#include "compiler/ir/poly_typed.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed/infer.h"
#include "compiler/opcode_cases.h"
#include <utility>

namespace
{
    using namespace monad::compiler::poly_typed;
    using namespace monad::compiler;

    struct TypeError
    {
    };

    ContKind get_literal_cont(PolyTypedIR const &ir, uint256_t const &literal)
    {
        if (literal > uint256_t{std::numeric_limits<byte_offset>::max()}) {
            return cont_kind({}, 0);
        }
        byte_offset off{literal[0]};
        auto jd = ir.jumpdests.find(off);
        if (jd == ir.jumpdests.end()) {
            return cont_kind({}, 0);
        }
        assert(jd->second < ir.blocks.size());
        return ir.blocks[jd->second].kind;
    }

    Kind get_param_kind(Block const &block, uint256_t const &param_id)
    {
        assert(param_id <= uint256_t{std::numeric_limits<size_t>::max()});
        size_t ix{param_id[0]};
        if (ix >= block.kind->front.size()) {
            throw TypeError{};
        }
        return block.kind->front[ix];
    }

    ContKind get_param_cont(Block const &block, uint256_t const &param_id)
    {
        auto k = get_param_kind(block, param_id);
        if (std::holds_alternative<Cont>(*k)) {
            return std::get<Cont>(*k).cont;
        }
        if (std::holds_alternative<WordCont>(*k)) {
            return std::get<WordCont>(*k).cont;
        }
        throw TypeError{};
    }

    void check_dest(PolyTypedIR const &ir, Block const &block,
            Value const &dest, ContKind kind)
    {
        if (dest.is == ValueIs::LITERAL) {
            if (!can_specialize(get_literal_cont(ir, dest.data), kind)) {
                throw TypeError{};
            }
        } else if (dest.is == ValueIs::PARAM_ID) {
            if (!weak_equal(get_param_cont(block, dest.data), kind)) {
                throw TypeError{};
            }
        } else {
            throw TypeError{};
        }
    }

    void check_output_value(PolyTypedIR const &ir, Block const &block,
            Value const &x, Kind k)
    {
        if (x.is == ValueIs::LITERAL) {
            std::visit(Cases{
                [&](LiteralVar const &lv) {
                    if (!can_specialize(get_literal_cont(ir, x.data), lv.cont)) {
                        throw TypeError{};
                    }
                },
                [&](Cont const &c) {
                    if (!can_specialize(get_literal_cont(ir, x.data), c.cont)) {
                        throw TypeError{};
                    }
                },
                [&](WordCont const &wc) {
                    if (!can_specialize(get_literal_cont(ir, x.data), wc.cont)) {
                        throw TypeError{};
                    }
                },
                [&](Word const &) {
                    // nop
                },
                [&](auto const &) {
                    throw TypeError{};
                }
            }, *k);
        }
        else if (x.is == ValueIs::PARAM_ID) {
            if (std::holds_alternative<Any>(*k)) {
                return;
            }
            auto p = get_param_kind(block, x.data);
            if (std::holds_alternative<WordCont>(*p) &&
                    !std::holds_alternative<WordCont>(*k)) {
                if (std::holds_alternative<Word>(*k)) {
                    return;
                }
                if (!std::holds_alternative<Cont>(*k)) {
                    throw TypeError{};
                }
                if (!weak_equal(std::get<WordCont>(*p).cont, std::get<Cont>(*k).cont)) {
                    throw TypeError{};
                }
            } else {
                if (!weak_equal(p, k)) {
                    throw TypeError{};
                }
            }
        }
        else {
            assert(x.is == ValueIs::COMPUTED);
            if (!weak_equal(k, word)) {
                throw TypeError{};
            }
        }
    }

    void check_output_stack(Block const &block, size_t output_offset,
            ContKind out_kind, std::vector<Kind> const &output_stack)
    {
        assert(block.output.size() >= output_offset);
        assert(output_stack.size() >= block.output.size());
        size_t min_size = std::min(
                output_stack.size() - output_offset,
                out_kind->front.size());
        for (size_t i = 0; i < min_size; ++i) {
            if (output_offset + i < block.output.size() &&
                    block.output[output_offset + i].is == ValueIs::LITERAL) {
                if (!weak_equal(output_stack[output_offset + i], word)) {
                    throw TypeError{};
                }
            }
            else {
                auto const &k1 = output_stack[output_offset + i];
                auto const &k2 = out_kind->front[i];
                if (std::holds_alternative<Any>(*k2)) {
                    continue;
                }
                if (std::holds_alternative<WordCont>(*k1)) {
                    if (std::holds_alternative<Cont>(*k2)) {
                        if (!weak_equal(std::get<WordCont>(*k1).cont, std::get<Cont>(*k2).cont)) {
                            throw TypeError{};
                        }
                    } else if (!std::holds_alternative<Word>(*k2)) {
                        if (!weak_equal(k1, k2)) {
                            throw TypeError{};
                        }
                    }
                } else {
                    if (!weak_equal(k1, k2)) {
                        throw TypeError{};
                    }
                }
            }
        }
        if (output_stack.size() != out_kind->front.size() + output_offset) {
            if (!std::holds_alternative<ContWords>(out_kind->tail)) {
                throw TypeError{};
            }
            if (!std::holds_alternative<ContWords>(block.kind->tail)) {
                throw TypeError{};
            }
        }
        for (size_t i = min_size + output_offset; i < output_stack.size(); ++i) {
            if (!weak_equal(output_stack[i], word)) {
                throw TypeError{};
            }
        }
        for (size_t i = min_size; i < out_kind->front.size(); ++i) {
            if (!weak_equal(out_kind->front[i], word)) {
                throw TypeError{};
            }
        }
    }

    void check_output(PolyTypedIR const &ir, Block const &block,
            size_t output_offset, ContKind out_kind,
            std::vector<Kind> const &output_stack)
    {
        check_output_stack(block, output_offset, out_kind, output_stack);
        assert(block.output.size() >= output_offset);
        size_t arg_count = block.output.size() - output_offset;
        std::vector<Kind> out_front = out_kind->front;
        if (out_front.size() < arg_count) {
            if (!std::holds_alternative<ContWords>(block.kind->tail)) {
                throw TypeError{};
            }
            do {
                out_front.push_back(word);
            } while (out_front.size() < arg_count);
        }

        for (size_t i = 0; i < arg_count; ++i) {
            check_output_value(ir, block,
                    block.output[output_offset + i], out_front[i]);
        }

        size_t n_left = out_front.size() - arg_count;
        if (block.kind->front.size() != n_left + block.min_params &&
                !std::holds_alternative<ContWords>(block.kind->tail)) {
            throw TypeError{};
        }
        std::vector<Kind> new_tail_front;
        for (size_t i = 0; i < n_left; ++i) {
            if (block.min_params + i < block.kind->front.size()) {
                new_tail_front.push_back(block.kind->front[block.min_params + i]);
            }
            else {
                new_tail_front.push_back(word);
            }
        }
        std::vector<Kind> new_out_front;
        for (size_t i = arg_count; i < out_front.size(); ++i) {
            new_out_front.push_back(out_front[i]);
        }
        if (!weak_equal(cont_kind(std::move(new_tail_front), block.kind->tail),
                cont_kind(std::move(new_out_front), out_kind->tail))) {
            throw TypeError{};
        }
    }

    void check_block_not_word_typed(Block const &block)
    {
        if (std::holds_alternative<ContWords>(block.kind->tail)) {
            throw TypeError{};
        }
        std::visit(Cases{
            [](Jump const &t) {
                if (std::holds_alternative<ContWords>(t.jump_kind->tail)) {
                    throw TypeError{};
                }
            },
            [](JumpI const &t) {
                if (std::holds_alternative<ContWords>(t.jump_kind->tail) ||
                        std::holds_alternative<ContWords>(t.fallthrough_kind->tail)) {
                    throw TypeError{};
                }
            },
            [](FallThrough const &t) {
                if (std::holds_alternative<ContWords>(t.fallthrough_kind->tail)) {
                    throw TypeError{};
                }
            },
            [](auto const &) {
                // nop
            }
        }, block.terminator);
    }

    void check_block_word_typed(Block const &block)
    {
        if (!weak_equal(block.kind, cont_words)) {
            throw TypeError{};
        }
        std::visit(Cases{
            [](Jump const &t) {
                if (!weak_equal(t.jump_kind, cont_words)) {
                    throw TypeError{};
                }
            },
            [](JumpI const &t) {
                if (!weak_equal(t.jump_kind, cont_words) || !weak_equal(t.fallthrough_kind, cont_words)) {
                    throw TypeError{};
                }
            },
            [](FallThrough const &t) {
                if (!weak_equal(t.fallthrough_kind, cont_words)) {
                    throw TypeError{};
                }
            },
            [](auto const &) {
                throw TypeError{};
            }
        }, block.terminator);
    }

    void check_instruction_pop(std::vector<Kind> &stack)
    {
        if (stack.empty()) {
            throw TypeError{};
        }
        stack.pop_back();
    }

    void
    check_instruction_swap(Instruction const &ins, std::vector<Kind> &stack)
    {
        size_t const ix = get_swap_opcode_index(ins.opcode);
        if (stack.size() <= ix) {
            throw TypeError{};
        }
        std::swap(stack[stack.size() - 1], stack[stack.size() - 1 - ix]);
    }

    void check_instruction_dup(Instruction const &ins, std::vector<Kind> &stack)
    {
        size_t const ix = get_dup_opcode_index(ins.opcode);
        if (stack.size() < ix) {
            throw TypeError{};
        }
        stack.push_back(stack[stack.size() - ix]);
    }

    void check_instruction_default(
        Instruction const &ins, std::vector<Kind> &stack)
    {
        auto const info = opcode_info_table[ins.opcode];
        if (stack.size() < info.min_stack) {
            throw TypeError{};
        }
        std::vector<Kind> const front;
        for (size_t i = 0; i < info.min_stack; ++i) {
            if (!std::holds_alternative<Word>(*stack.back()) &&
                    !std::holds_alternative<WordCont>(*stack.back())) {
                throw TypeError{};
            }
            stack.pop_back();
        }
        if (info.increases_stack) {
            stack.push_back(word);
        }
    }

    void check_instruction(Instruction const &ins, std::vector<Kind> &stack)
    {
        switch (ins.opcode) {
        case POP:
            return check_instruction_pop(stack);
        case ANY_SWAP:
            return check_instruction_swap(ins, stack);
        case ANY_DUP:
            return check_instruction_dup(ins, stack);
        default:
            return check_instruction_default(ins, stack);
        }
    }

    std::vector<Kind> check_instructions(Block const &block) {
        std::vector<Kind> stack = block.kind->front;
        if (std::holds_alternative<ContWords>(block.kind->tail)) {
            while (stack.size() < block.min_params) {
                stack.push_back(word);
            }
        }
        std::reverse(stack.begin(), stack.end());
        for (auto const &ins : block.instrs) {
            check_instruction(ins, stack);
        }
        std::reverse(stack.begin(), stack.end());
        return stack;
    }

    void check_block_exact(PolyTypedIR const &ir, Block const &block)
    {
        std::vector<Kind> output_stack = check_instructions(block);
        if (std::holds_alternative<Jump>(block.terminator)) {
            assert(!block.output.empty());
            auto &jump = std::get<Jump>(block.terminator);
            check_dest(ir, block, block.output[0], jump.jump_kind);
            check_output(ir, block, 1, jump.jump_kind, output_stack);
        } else if (std::holds_alternative<JumpI>(block.terminator)) {
            assert(block.output.size() >= 2);
            auto &jumpi = std::get<JumpI>(block.terminator);
            check_dest(ir, block, block.output[0], jumpi.jump_kind);
            check_output(ir, block, 2, jumpi.jump_kind, output_stack);
            if (!can_specialize(ir.blocks[jumpi.fallthrough_dest].kind, jumpi.fallthrough_kind)) {
                throw TypeError{};
            }
            check_output(ir, block, 2, jumpi.fallthrough_kind, output_stack);
        } else if (std::holds_alternative<FallThrough>(block.terminator)) {
            auto &fall = std::get<FallThrough>(block.terminator);
            if (!can_specialize(ir.blocks[fall.fallthrough_dest].kind, fall.fallthrough_kind)) {
                throw TypeError{};
            }
            check_output(ir, block, 0, fall.fallthrough_kind, output_stack);
        } else {
            // It should not be possible to have Word.. tail with "exit" terminator:
            check_block_not_word_typed(block);
        }
    }

    void check_block(PolyTypedIR const &ir, Block const &block)
    {
        try {
            check_block_exact(ir, block);
        }
        catch (TypeError const &) {
            // TypeError is OK if check_block_word_typed goes through:
            check_block_word_typed(block);
        }
    }
}

namespace monad::compiler::poly_typed
{
    PolyTypedIR::PolyTypedIR(local_stacks::LocalStacksIR const &&ir)
        : codesize{ir.codesize}
        , jumpdests{std::move(ir.jumpdests)}
        , blocks{infer_types(jumpdests, ir.blocks)}
    {
    }

    bool PolyTypedIR::type_check()
    {
        try {
            for (auto const &b : blocks) {
                check_block(*this, b);
            }
        }
        catch (TypeError const &) {
            return false;
        }
        return true;
    }
}
