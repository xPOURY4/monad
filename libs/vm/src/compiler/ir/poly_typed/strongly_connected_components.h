#pragma once

#include "infer_state.h"

namespace monad::compiler::poly_typed
{
    using Component = std::vector<block_id>;

    struct TarjanVertexState
    {
        size_t index;
        size_t lowlink;
        bool on_stack;
        bool is_defined;
    };

    struct TarjanState
    {
        InferState const &infer_state;
        size_t index;
        std::vector<block_id> stack;
        std::vector<TarjanVertexState> vertex_states;
        std::vector<Component> components;
    };

    struct ConnectBlocks
    {
        block_id block;
        block_id parent;
        bool visited;
    };

    void strong_connect(TarjanState &state, block_id block)
    {
        std::vector<ConnectBlocks> connect_stack{{block, block, false}};

        while (!connect_stack.empty()) {
            auto b = connect_stack.back().block;

            auto &bst = state.vertex_states[b];

            if (connect_stack.back().visited) {
                auto &pst = state.vertex_states[connect_stack.back().parent];

                connect_stack.pop_back();

                if (bst.lowlink == bst.index) {
                    state.components.emplace_back();
                    block_id t;
                    do {
                        t = state.stack.back();
                        state.stack.pop_back();
                        state.components.back().push_back(t);
                        state.vertex_states[t].on_stack = false;
                    }
                    while (b != t);
                }

                pst.lowlink = std::min(pst.lowlink, bst.lowlink);
            }
            else {
                connect_stack.back().visited = true;

                state.vertex_states[b] = {state.index, state.index, true, true};
                ++state.index;
                state.stack.push_back(b);

                auto succs = static_successors(state.infer_state, b);
                for (auto s : succs) {
                    auto &sst = state.vertex_states[s];
                    if (!sst.is_defined) {
                        connect_stack.push_back({s, b, false});
                    }
                    else if (sst.on_stack) {
                        bst.lowlink = std::min(bst.lowlink, sst.index);
                    }
                }
            }
        }
    }

    std::vector<Component>
    strongly_connected_components(InferState const &infer_state)
    {
        // Tarjan's algorithm, but without recursive function calls
        TarjanState state{
            .infer_state = infer_state,
            .index = 0,
            .stack = {},
            .vertex_states = std::vector<TarjanVertexState>(
                infer_state.pre_blocks.size(), {0, 0, false, false}),
            .components = {}};
        for (block_id b = 0; b < state.vertex_states.size(); ++b) {
            if (!state.vertex_states[b].is_defined) {
                strong_connect(state, b);
            }
        }
        return std::move(state.components);
    }
}
