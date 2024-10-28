#include "compiler/ir/poly_typed.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed/infer.h"
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
            if (get_literal_cont(ir, dest.data) > kind) {
                throw TypeError{};
            }
        } else if (dest.is == ValueIs::PARAM_ID) {
            if (get_param_cont(block, dest.data) != kind) {
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
                    if (get_literal_cont(ir, x.data) > lv.cont) {
                        throw TypeError{};
                    }
                },
                [&](Cont const &c) {
                    if (get_literal_cont(ir, x.data) > c.cont) {
                        throw TypeError{};
                    }
                },
                [&](WordCont const &wc) {
                    if (get_literal_cont(ir, x.data) > wc.cont) {
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
                if (std::get<WordCont>(*p).cont != std::get<Cont>(*k).cont) {
                    throw TypeError{};
                }
            } else {
                if (p != k) {
                    throw TypeError{};
                }
            }
        }
        else {
            assert(x.is == ValueIs::COMPUTED);
            if (k != word) {
                throw TypeError{};
            }
        }
    }

    void check_output(PolyTypedIR const &ir, Block const &block,
            size_t output_offset, ContKind out_kind)
    {
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
        if (cont_kind(std::move(new_tail_front), block.kind->tail) !=
                cont_kind(std::move(new_out_front), out_kind->tail)) {
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
        if (block.kind != cont_words) {
            throw TypeError{};
        }
        std::visit(Cases{
            [](Jump const &t) {
                if (t.jump_kind != cont_words) {
                    throw TypeError{};
                }
            },
            [](JumpI const &t) {
                if (t.jump_kind != cont_words || t.fallthrough_kind != cont_words) {
                    throw TypeError{};
                }
            },
            [](FallThrough const &t) {
                if (t.fallthrough_kind != cont_words) {
                    throw TypeError{};
                }
            },
            [](auto const &) {
                throw TypeError{};
            }
        }, block.terminator);
    }

    void check_block_exact(PolyTypedIR const &ir, Block const &block)
    {
        if (std::holds_alternative<Jump>(block.terminator)) {
            assert(!block.output.empty());
            auto &jump = std::get<Jump>(block.terminator);
            check_dest(ir, block, block.output[0], jump.jump_kind);
            check_output(ir, block, 1, jump.jump_kind);
        } else if (std::holds_alternative<JumpI>(block.terminator)) {
            assert(block.output.size() >= 2);
            auto &jumpi = std::get<JumpI>(block.terminator);
            check_dest(ir, block, block.output[0], jumpi.jump_kind);
            check_output(ir, block, 2, jumpi.jump_kind);
            if (ir.blocks[jumpi.fallthrough_dest].kind > jumpi.fallthrough_kind) {
                throw TypeError{};
            }
            check_output(ir, block, 2, jumpi.fallthrough_kind);
        } else if (std::holds_alternative<FallThrough>(block.terminator)) {
            auto &fall = std::get<FallThrough>(block.terminator);
            if (ir.blocks[fall.fallthrough_dest].kind > fall.fallthrough_kind) {
                throw TypeError{};
            }
            check_output(ir, block, 0, fall.fallthrough_kind);
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
