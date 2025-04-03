#pragma once

#include <monad/compiler/ir/basic_blocks.hpp>
#include <monad/compiler/ir/x86.hpp>
#include <monad/vm/core/assert.h>

#include <asmjit/x86.h>
#include <evmc/evmc.h>
#include <valgrind/cachegrind.h>

#include <cstdint>
#include <optional>

template <bool instrument>
class InstrumentableCompiler
{
public:
    InstrumentableCompiler(asmjit::JitRuntime &rt)
        : rt_(rt)
    {
    }

    std::optional<monad::compiler::native::entrypoint_t> compile(
        evmc_revision rev,
        monad::compiler::basic_blocks::BasicBlocksIR const &ir)
    {
        if constexpr (instrument) {
            CACHEGRIND_START_INSTRUMENTATION;
            auto ans =
                monad::compiler::native::compile_basic_blocks(rev, rt_, ir);
            CACHEGRIND_STOP_INSTRUMENTATION;
            return ans;
        }
        else {
            return monad::compiler::native::compile_basic_blocks(rev, rt_, ir);
        }
    }

private:
    asmjit::JitRuntime &rt_;
};
