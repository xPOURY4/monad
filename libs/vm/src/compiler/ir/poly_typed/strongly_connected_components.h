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

    void strong_connect(TarjanState &state, block_id block);

    std::vector<Component>
    strongly_connected_components(InferState const &infer_state);
}
