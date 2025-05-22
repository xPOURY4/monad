#pragma once

#include <monad/vm/compiler/ir/basic_blocks.hpp>
#include <monad/vm/core/assert.h>
#include <stopwatch.hpp>

#include <evmc/evmc.h>
#include <valgrind/cachegrind.h>

#include <cstdint>
#include <vector>

template <bool instrument>
class InstrumentableParser
{
public:
    template <evmc_revision Rev>
    std::optional<monad::vm::compiler::basic_blocks::BasicBlocksIR>
    parse(std::vector<uint8_t> const &code)
    {
        if constexpr (instrument) {
            timer.start();
            CACHEGRIND_START_INSTRUMENTATION;
            auto ir = monad::vm::compiler::basic_blocks::BasicBlocksIR(
                monad::vm::compiler::basic_blocks::make_ir<Rev>(code));
            CACHEGRIND_STOP_INSTRUMENTATION;
            timer.pause();
            return ir;
        }
        else {
            return monad::vm::compiler::basic_blocks::BasicBlocksIR(
                monad::vm::compiler::basic_blocks::make_ir<Rev>(code));
        }
    }
};
