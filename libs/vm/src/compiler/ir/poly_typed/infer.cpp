#include "compiler/ir/poly_typed/infer.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/ir/poly_typed/exceptions.h"
#include "compiler/ir/poly_typed/infer_state.h"
#include "compiler/ir/poly_typed/kind.h"
#include "compiler/ir/poly_typed/strongly_connected_components.h"
#include "compiler/types.h"
#include <compiler/opcode_cases.h>
#include <cassert>
#include <exception>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::poly_typed;

    void insert_block_terminator(InferState &state, block_id bid, Terminator &&term)
    {
        auto new_term = std::visit(Cases{
            [&state](FallThrough const& t) -> Terminator {
                return FallThrough{state.subst_maps.back().subst_or_throw(t.fall_through_kind)};
            },
            [&state](JumpI const& t) -> Terminator {
                return JumpI{
                    .fall_through_kind = state.subst_maps.back().subst_or_throw(t.fall_through_kind),
                    .jump_kind = state.subst_maps.back().subst_or_throw(t.jump_kind)
                };
            },
            [&state](Jump const& t) -> Terminator {
                auto jkind = state.subst_maps.back().subst_or_throw(t.jump_kind);
                return Jump{};
            },
            [](Return const& t) -> Terminator {
                return t;
            },
            [](Stop const& t) -> Terminator {
                return t;
            },
            [](Revert const& t) -> Terminator {
                return t;
            },
            [](SelfDestruct const& t) -> Terminator {
                return t;
            },
            [](InvalidInstruction const& t) -> Terminator {
                return t;
            },
        }, term);

        state.block_terminators.insert_or_assign(bid, std::move(new_term));
    }

    ContKind initial_block_kind(InferState &state, block_id bid)
    {
        std::vector<Kind> front;
        size_t n = state.pre_blocks[bid].min_params;
        for (size_t i = 0; i < n; ++i) {
            front.push_back(kind_var(state.fresh()));
        }
        return cont_kind(std::move(front), state.fresh());
    }

    void infer_instruction_pop(std::vector<Kind> &stack)
    {
        assert(!stack.empty());
        stack.pop_back();
    }

    void infer_instruction_swap(Instruction const &ins, std::vector<Kind> &stack)
    {
        size_t ix = get_swap_opcode_index(ins.opcode);
        assert(stack.size() > ix);
        std::swap(stack[stack.size() - 1], stack[stack.size() - 1 - ix]);
    }

    void infer_instruction_dup(Instruction const &ins,
            std::vector<Kind> &stack)
    {
        size_t ix = get_dup_opcode_index(ins.opcode);
        assert(stack.size() >= ix);
        stack.push_back(stack[stack.size() - ix]);
    }

    void infer_instruction_default(InferState &state, Instruction const &ins,
            std::vector<Kind> &stack)
    {
        auto const info = opcode_info_table[ins.opcode];
        assert(stack.size() >= info.min_stack);
        std::vector<Kind> front;
        for (size_t i = 0; i < info.min_stack; ++i) {
            unify(state.subst_maps.back(), stack.back(), word);
            stack.pop_back();
        }
        if (info.increases_stack) {
            stack.push_back(word);
        }
    }

    void infer_instruction(InferState &state, Instruction const &ins,
            std::vector<Kind> &stack)
    {
        switch (ins.opcode) {
        case POP:
            return infer_instruction_pop(stack);
        case ANY_SWAP:
            return infer_instruction_swap(ins, stack);
        case ANY_DUP:
            return infer_instruction_dup(ins, stack);
        default:
            return infer_instruction_default(state, ins, stack);
        }
    }

    void unify_terminator_input(InferState &state, basic_blocks::Terminator term,
            std::vector<Kind> &stack)
    {
        switch (term) {
        case basic_blocks::Terminator::JumpI:
            assert(stack.size() >= 2);
            unify(state.subst_maps.back(), stack[stack.size() - 2], word);
            break;
        case basic_blocks::Terminator::Return:
            // Fall through
        case basic_blocks::Terminator::Revert:
            assert(stack.size() >= 2);
            unify(state.subst_maps.back(), stack[stack.size() - 1], word);
            unify(state.subst_maps.back(), stack[stack.size() - 2], word);
            break;
        case basic_blocks::Terminator::SelfDestruct:
            assert(!stack.empty());
            unify(state.subst_maps.back(), stack.back(), word);
            break;
        default:
            break;
        }
    }

    void infer_block_start(InferState &state, block_id bid)
    {
        ContKind cont = state.block_types.at(bid);
        std::vector<Kind> stack = cont->front;
        auto const &block = state.pre_blocks[bid];
        for (auto const &ins : block.instrs) {
            infer_instruction(state, ins, stack);
        }
        unify_terminator_input(state, block.terminator, stack);
    }

    Terminator infer_block_end(InferState &state, block_id bid, Component const &component)
    {
        // XXX always consider recursive literals as words if used as argument
        // to parameter jump.
        (void)state;
        (void)bid;
        (void)component;
        auto new_kind = state.subst_maps.back().subst_or_throw(state.block_types.at(bid));
        state.block_types.insert_or_assign(bid, std::move(new_kind));
        std::terminate();
    }

    void infer_recursive_terminator(InferState &state, Component const &component)
    {
        for (block_id bid : component) {
            infer_block_end(state, bid, component);
        }
        for (block_id bid : component) {
            infer_block_end(state, bid, component);
        }
        for (block_id bid : component) {
            auto orig_type = state.block_types.at(bid);
            auto term = infer_block_end(state, bid, component);
            insert_block_terminator(state, bid, std::move(term));
            auto new_type = state.block_types.at(bid);
            if (!alpha_equal(std::move(orig_type), std::move(new_type))) {
                throw UnificationException{};
            }
        }
    }

    void infer_non_recursive_terminator(InferState &state, Component const &component)
    {
        assert(component.size() == 1);
        block_id bid = *component.begin();
        auto term = infer_block_end(state, bid, component);
        insert_block_terminator(state, bid, std::move(term));
    }

    void set_word_typed_component(InferState &state, Component const &component)
    {
        (void)state;
        (void)component;
    }

    bool is_recursive_component(InferState &state, Component const &component)
    {
        if (component.size() > 1) {
            return true;
        }
        for (block_id suc : state.static_successors(*component.begin())) {
            if (component.contains(suc)) {
                return true;
            }
        }
        return false;
    }

    void infer_component(InferState &state, Component const &component)
    {
        assert(!component.empty());
        state.subst_maps.emplace_back();
        state.next_fresh_var_names.push_back(0);
        for (auto const &bid : component) {
            __attribute__((unused)) bool const ins =
                state.block_types
                    .insert_or_assign(bid, initial_block_kind(state, bid))
                    .second;
            assert(ins);
        }
        try {
            for (auto const &bid : component) {
               infer_block_start(state, bid);
            }
            if (is_recursive_component(state, component)) {
                infer_recursive_terminator(state, component);
            } else {
                infer_non_recursive_terminator(state, component);
            }
        }
        catch (InferException const &) {
            set_word_typed_component(state, component);
        }
        state.next_fresh_var_names.pop_back();
        state.subst_maps.pop_back();
    }

    std::vector<Block> infer_components(
        InferState &state, std::vector<Component> const &components)
    {
        for (auto const &c : components) {
            infer_component(state, c);
        }
        std::vector<Block> blocks;
        for (block_id i = 0; i < state.pre_blocks.size(); ++i) {
            auto const &pre_block = state.pre_blocks[i];
            blocks.push_back(Block{
                .output = std::move(pre_block.output),
                .instrs = std::move(pre_block.instrs),
                .kind = std::move(state.block_types.at(i)),
                .terminator = std::move(state.block_terminators.at(i))
            });
        }
        return blocks;
    }
}

namespace monad::compiler::poly_typed
{
    std::vector<Block> infer_types(
        std::unordered_map<byte_offset, block_id> const &jumpdests,
        std::vector<local_stacks::Block> const &pre_blocks)
    {
        InferState state{
            .jumpdests = jumpdests,
            .pre_blocks = pre_blocks,
            .next_fresh_var_names = {},
            .subst_maps = {},
            .block_types = {},
            .block_terminators = {},
        };
        auto const components = strongly_connected_components(state);
        return infer_components(state, components);
    }
}
