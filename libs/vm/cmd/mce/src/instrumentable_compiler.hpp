#pragma once

#include <monad/vm/compiler/ir/basic_blocks.hpp>
#include <monad/vm/compiler/ir/x86.hpp>
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
    InstrumentableCompiler(
        asmjit::JitRuntime &rt,
        monad::vm::compiler::native::CompilerConfig const &config)
        : rt_(rt)
        , config_(config)
    {
    }

    std::shared_ptr<monad::vm::compiler::native::Nativecode> compile(
        evmc_revision rev,
        monad::vm::compiler::basic_blocks::BasicBlocksIR const &ir)
    {
        if constexpr (instrument) {
            CACHEGRIND_START_INSTRUMENTATION;
            auto ans = monad::vm::compiler::native::compile_basic_blocks(
                rev, rt_, ir, config_);
            CACHEGRIND_STOP_INSTRUMENTATION;
            return ans;
        }
        else {
            return monad::vm::compiler::native::compile_basic_blocks(
                rev, rt_, ir, config_);
        }
    }

private:
    asmjit::JitRuntime &rt_;
    monad::vm::compiler::native::CompilerConfig const &config_;
};
