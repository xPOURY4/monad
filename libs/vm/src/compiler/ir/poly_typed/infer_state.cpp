#include "infer_state.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/types.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace monad::compiler::poly_typed
{
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

        byte_offset const offset = static_cast<byte_offset>(value.data[0]);

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
