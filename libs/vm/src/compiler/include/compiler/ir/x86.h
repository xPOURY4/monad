#pragma once

#include <compiler/ir/local_stacks.h>
#include <runtime/runtime.h>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <optional>
#include <span>

namespace monad::compiler::native
{
    using entrypoint_t =
        void (*)(runtime::Result *, runtime::Context *, uint8_t *);

    /**
     * Compile the given contract and add it to JitRuntime. On success
     * the contract main functions is returned. Returns null on error.
     */
    std::optional<entrypoint_t> compile(
        asmjit::JitRuntime &rt, std::span<uint8_t const> contract,
        evmc_revision rev);
}
