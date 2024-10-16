#include "compiler/ir/poly_typed/infer.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/ir/poly_typed/exceptions.h"
#include "compiler/ir/poly_typed/infer_state.h"
#include "compiler/ir/poly_typed/kind.h"
#include "compiler/ir/poly_typed/strongly_connected_components.h"
#include "compiler/types.h"
#include <cassert>
#include <exception>
#include <unordered_map>
#include <utility>
#include <vector>

namespace monad::compiler::poly_typed
{
    std::pair<ContKind, Terminator> infer_block(InferState &state, block_id bid)
    {
        (void)state;
        (void)bid;
        std::terminate(); // TODO
    }

    std::vector<Block>
    infer_component(InferState &state, Component const &component)
    {
        state.subst_maps.emplace_back();
        state.next_fresh_var_names.push_back(0);
        for (auto const &bid : component) {
            __attribute__((unused)) bool const ins =
                state.block_types
                    .insert_or_assign(bid, cont_kind({}, state.fresh()))
                    .second;
            assert(ins);
        }
        /// XXX always consider recursive literals as words if used as argument
        /// to parameter jump.
        try {
            for (auto const &bid : component) {
                auto [c, t] = infer_block(state, bid);
                __attribute__((unused)) bool const ins1 =
                    state.block_types.insert_or_assign(bid, std::move(c))
                        .second;
                assert(!ins1);
                __attribute__((unused)) bool const ins2 =
                    state.block_terminators.insert_or_assign(bid, std::move(t))
                        .second;
                assert(ins2);
            }
            // Infer type of each block in component.
            // Update type of component blocks accordingly.
            // If there are not recursive jump literals, then done.
            // If there is a recursive jump literal:
            //  Re-infer types for each block
            //  Re-re-infer types. If types changed this time, then unification
            //  error, else done.
        }
        catch (InferException const &) {
            for (auto const &bid : component) {
                __attribute__((unused)) bool const ins =
                    state.block_types.insert_or_assign(bid, cont_words).second;
                assert(!ins);
                // TODO update output types to cont_words as well
            }
        }
        state.next_fresh_var_names.pop_back();
        state.subst_maps.pop_back();
        return {};
    }

    std::vector<Block> infer_components(
        InferState &state, std::vector<Component> const &components)
    {
        std::vector<Block> blocks;
        for (auto const &c : components) {
            auto new_blocks = infer_component(state, c);
            for (auto &new_block : new_blocks) {
                blocks.push_back(std::move(new_block));
            }
        }
        return blocks;
    }

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
