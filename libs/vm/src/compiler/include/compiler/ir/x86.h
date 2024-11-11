#pragma once

#include <compiler/ir/local_stacks.h>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <span>

namespace monad::compiler::native
{
    using entrypoint_t = void (*)();

    /**
     * Compile the given contract into an asmjit code buffer.
     *
     * The caller is responsible for managing the surrounding context of the
     * given buffer, and the lifetime of the compilation result (by adding it to
     * a JIT runtime context).
     */
    void compile(
        asmjit::CodeHolder &into, std::span<uint8_t const> contract,
        evmc_revision rev);
}
