#include "compiler/ir/poly_typed/infer_state.h"
#include "compiler/ir/basic_blocks.h"
#include "compiler/ir/local_stacks.h"
#include "compiler/ir/poly_typed/block.h"
#include "compiler/ir/poly_typed/kind.h"
#include "compiler/types.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    using namespace monad::compiler;
    using namespace monad::compiler::poly_typed;

    void push_static_jumpdest(
        std::vector<block_id> &dest, InferState const &state,
        Value const &value)
    {
        auto d = state.get_jumpdest(value);
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

    Kind refresh(InferState &state, PolyVarSubstMap &su, Kind cont);

    ContKind refresh(InferState &state, PolyVarSubstMap &su, ContKind cont)
    {
        std::vector<Kind> kinds;
        for (auto &k : cont->front) {
            kinds.push_back(refresh(state, su, k));
        }
        return std::visit(
            Cases{
                [&state, &su, &kinds](ContVar const &cv) {
                    VarName new_v;
                    auto it = su.cont_map.find(cv.var);
                    if (it != su.cont_map.end()) {
                        new_v = it->second;
                    }
                    else {
                        new_v = state.fresh_cont_var();
                        su.cont_map.insert_or_assign(cv.var, new_v);
                    }
                    return cont_kind(std::move(kinds), new_v);
                },
                [&kinds](ContWords const &) {
                    return cont_kind(std::move(kinds));
                },
            },
            cont->tail);
    }

    Kind refresh(InferState &state, PolyVarSubstMap &su, Kind kind)
    {
        return std::visit(
            Cases{
                [](Word const &) { return word; },
                [](Any const &) { return any; },
                [&state, &su](KindVar const &kv) {
                    VarName new_v;
                    auto it = su.kind_map.find(kv.var);
                    if (it != su.kind_map.end()) {
                        new_v = it->second;
                    }
                    else {
                        new_v = state.fresh_kind_var();
                        su.kind_map.insert_or_assign(kv.var, new_v);
                    }
                    return kind_var(new_v);
                },
                [&kind](LiteralVar const &) { return std::move(kind); },
                [&state, &su](WordCont const &wc) {
                    return word_cont(refresh(state, su, wc.cont));
                },
                [&state, &su](Cont const &c) {
                    return cont(refresh(state, su, c.cont));
                }},
            *kind);
    }
}

namespace monad::compiler::poly_typed
{
    InferState::InferState(
        std::unordered_map<byte_offset, block_id> const &j,
        std::vector<local_stacks::Block> const &b)
        : jumpdests{j}
        , pre_blocks{b}
        , next_cont_var_name{}
        , next_kind_var_name{}
        , next_literal_var_name{}
    {
    }

    void InferState::reset()
    {
        subst_map.reset();
        next_cont_var_name = 0;
        next_kind_var_name = 0;
    }

    ContKind InferState::get_type(block_id bid)
    {
        auto it = block_types.find(bid);
        assert(it != block_types.end());
        PolyVarSubstMap su;
        return refresh(*this, su, it->second);
    }

    std::optional<block_id> InferState::get_jumpdest(Value const &value) const
    {
        if (value.is != ValueIs::LITERAL) {
            return std::nullopt;
        }
        if (value.data > uint256_t{std::numeric_limits<byte_offset>::max()}) {
            return std::nullopt;
        }

        static_assert(sizeof(byte_offset) <= sizeof(uint64_t));

        byte_offset const offset = static_cast<byte_offset>(value.data[0]);

        auto it = jumpdests.find(offset);
        if (it == jumpdests.end()) {
            return std::nullopt;
        }
        return it->second;
    }

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
            push_static_jumpdests(
                ret, *this, &block.output[0], block.output.size());
            break;
        default:
            break;
        }
        return ret;
    }
}
