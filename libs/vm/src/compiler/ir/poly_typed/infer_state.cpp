#include "compiler/ir/poly_typed/infer_state.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/poly_typed.h"
#include "compiler/types.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::poly_typed;

    std::optional<block_id>
    get_jumpdest(InferState const &state, Value const &value)
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

    void push_static_jumpdest(
        std::vector<block_id> &dest, InferState const &state,
        Value const &value)
    {
        auto d = get_jumpdest(state, value);
        if (d.has_value()) {
            dest.push_back(d.value());
        }
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
    std::vector<block_id> InferState::static_successors(block_id b) const
    {
        std::vector<block_id> ret;
        auto const &block = pre_blocks[b];
        switch (block.terminator) {
        case basic_blocks::Terminator::JumpDest:
            ret.push_back(block.fallthrough_dest);
            push_static_jumpdests(
                ret, *this, &block.output[0], block.output.size());
            break;
        case basic_blocks::Terminator::JumpI:
            assert(block.output.size() >= 2);
            ret.push_back(block.fallthrough_dest);
            push_static_jumpdest(ret, *this, block.output[0]);
            push_static_jumpdests(
                ret, *this, &block.output[2], block.output.size() - 2);
            break;
        case basic_blocks::Terminator::Jump:
            assert(block.output.size() >= 1);
            if (!get_jumpdest(*this, block.output[0]).has_value()) {
                // We do not consider the output values if the jumpdest is not a
                // literal.
                break;
            }
            push_static_jumpdests(
                ret, *this, &block.output[0], block.output.size());
            break;
        default:
            break;
        }
        return ret;
    }
}
