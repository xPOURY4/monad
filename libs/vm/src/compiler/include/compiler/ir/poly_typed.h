#pragma once

#include "poly_typed/block.h"

namespace monad::compiler::poly_typed
{
    struct PolyTypedIR
    {
        PolyTypedIR(local_stacks::LocalStacksIR const &&ir);

        void type_check_or_throw();
        bool type_check();

        uint64_t codesize;
        std::unordered_map<byte_offset, block_id> jumpdests;
        std::vector<Block> blocks;
    };
}

template <>
struct std::formatter<monad::compiler::poly_typed::PolyTypedIR>
{
    constexpr auto parse(std::format_parse_context &ctx)
    {
        return ctx.begin();
    }

    std::format_context::iterator format(
        monad::compiler::poly_typed::PolyTypedIR const &ir,
        std::format_context &ctx) const;
};
