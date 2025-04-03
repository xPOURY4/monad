#pragma once

#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/vm/core/assert.h>

#include <evmc/evmc.h>
#include <valgrind/cachegrind.h>

#include <cstdint>
#include <vector>

template <bool instrument>
class InstrumentableParser
{
public:
    template <evmc_revision Rev>
    std::optional<monad::compiler::basic_blocks::BasicBlocksIR>
    parse(std::vector<uint8_t> const &code)
    {
        if constexpr (instrument) {
            CACHEGRIND_START_INSTRUMENTATION;
            auto ir = monad::compiler::basic_blocks::BasicBlocksIR(
                monad::compiler::basic_blocks::make_ir<Rev>(code));
            CACHEGRIND_STOP_INSTRUMENTATION;
            return ir;
        }
        else {
            return monad::compiler::basic_blocks::BasicBlocksIR(
                monad::compiler::basic_blocks::make_ir<Rev>(code));
        }
    }
};
