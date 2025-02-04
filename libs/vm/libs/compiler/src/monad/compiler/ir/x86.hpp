#pragma once

#include <monad/compiler/ir/local_stacks.hpp>
#include <monad/runtime/runtime.hpp>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <optional>
#include <span>

namespace monad::compiler::native
{
    using entrypoint_t = void (*)(runtime::Context *, uint8_t *);

    /**
     * Compile the given contract and add it to JitRuntime. On success
     * the contract main function is returned. Returns null on error.
     */
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev, char const *asm_log);
}
