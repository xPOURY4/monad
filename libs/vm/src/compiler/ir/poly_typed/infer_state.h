#pragma once

#include "subst_map.h"

#include <optional>
#include <unordered_map>

namespace monad::compiler::poly_typed
{
    struct InferState
    {
        std::unordered_map<byte_offset, block_id> const &jumpdests;
        std::vector<local_stacks::Block> const &pre_blocks;
        VarName next_fresh_var_name;
        std::vector<SubstMap> subst_maps;
        std::unordered_map<byte_offset, ContKind> block_types;
    };

    std::optional<block_id>
    static_jumpdests(InferState const &state, Value const &value)
    {
        if (value.is != ValueIs::LITERAL) {
            return std::nullopt;
        }
        if (value.data > uint256_t{std::numeric_limits<byte_offset>::max()}) {
            return std::nullopt;
        }

        static_assert(sizeof(byte_offset) <= sizeof(uint64_t));

        byte_offset offset = static_cast<byte_offset>(value.data[0]);

        auto it = state.jumpdests.find(offset);
        if (it == state.jumpdests.end()) {
            return std::nullopt;
        }

        return it->second;
    }

    std::vector<block_id> static_successors(InferState const &state, block_id b)
    {
        auto const &block = state.pre_blocks[b];
        switch (block.terminator) {
        case basic_blocks::Terminator::JumpDest:
            return {block.fallthrough_dest};
        case basic_blocks::Terminator::JumpI: {
            auto di = static_jumpdests(state, block.output[0]);
            return di.has_value()
                       ? std::vector{di.value(), block.fallthrough_dest}
                       : std::vector{block.fallthrough_dest};
        }
        case basic_blocks::Terminator::Jump: {
            auto d = static_jumpdests(state, block.output[0]);
            return d.has_value() ? std::vector{d.value()}
                                 : std::vector<block_id>{};
        }
        default:
            return {};
        }
    }
}
