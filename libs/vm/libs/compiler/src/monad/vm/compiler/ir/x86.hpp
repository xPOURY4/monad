#pragma once

#include <monad/vm/compiler/ir/local_stacks.hpp>
#include <monad/vm/compiler/ir/x86/types.hpp>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <memory>
#include <optional>
#include <span>

namespace monad::vm::compiler::native
{
    /**
     * Compile the given contract and add it to JitRuntime.
     */
    std::shared_ptr<Nativecode> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev, CompilerConfig const & = {});

    /**
     * Compile given IR and add it to the JitRuntime.
     */
    std::shared_ptr<Nativecode> compile_basic_blocks(
        evmc_revision rev, asmjit::JitRuntime &rt,
        basic_blocks::BasicBlocksIR const &ir, CompilerConfig const & = {});
}
