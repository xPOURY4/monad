#include "infer_state.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/types.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::poly_typed;

    void push_static_jumpdest(
        std::vector<block_id> &dest, InferState const &state,
        Value const &value)
    {
        if (value.is != ValueIs::LITERAL) {
            return;
        }
        if (value.data > uint256_t{std::numeric_limits<byte_offset>::max()}) {
            return;
        }

        static_assert(sizeof(byte_offset) <= sizeof(uint64_t));

        byte_offset const offset = static_cast<byte_offset>(value.data[0]);

        auto it = state.jumpdests.find(offset);
        if (it == state.jumpdests.end()) {
            return;
        }

        dest.push_back(it->second);
    }

    void push_static_jumpdests(
        std::vector<block_id> &dest, InferState const &state, Value const *tail,
        size_t tail_size)
    {
        for (size_t i = 0; i < tail_size; ++i) {
            push_static_jumpdest(dest, state, tail[i]);
        }
    }
}

namespace monad::compiler::poly_typed
{
    std::vector<block_id> static_successors(InferState const &state, block_id b)
    {
        std::vector<block_id> ret;
        auto const &block = state.pre_blocks[b];
        switch (block.terminator) {
        case basic_blocks::Terminator::JumpDest:
            ret.push_back(block.fallthrough_dest);
            push_static_jumpdests(
                ret, state, &block.output[0], block.output.size());
            break;
        case basic_blocks::Terminator::JumpI:
            assert(block.output.size() >= 2);
            ret.push_back(block.fallthrough_dest);
            push_static_jumpdest(ret, state, block.output[0]);
            push_static_jumpdests(
                ret, state, &block.output[2], block.output.size() - 2);
            break;
        case basic_blocks::Terminator::Jump:
            push_static_jumpdests(
                ret, state, &block.output[0], block.output.size());
            break;
        default:
            break;
        }
        return ret;
    }
}
