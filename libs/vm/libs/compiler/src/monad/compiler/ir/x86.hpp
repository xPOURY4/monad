#pragma once

#include <monad/compiler/ir/local_stacks.hpp>
#include <monad/compiler/ir/x86/types.hpp>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <optional>
#include <span>

namespace monad::compiler::native
{
    /**
     * Compile the given contract and add it to JitRuntime. On success
     * the contract main function is returned. Returns null on error.
     */
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev, CompilerConfig const & = {});

    /**
     * Compile given IR and add it to the JitRuntime. On success the
     * contract main function is returned. Returns null on error.
     */
    std::optional<entrypoint_t> compile_basic_blocks(
        evmc_revision rev, asmjit::JitRuntime &rt,
        basic_blocks::BasicBlocksIR const &ir, CompilerConfig const & = {});
}
