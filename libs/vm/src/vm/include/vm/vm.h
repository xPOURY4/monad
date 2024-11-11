#pragma once

#include <runtime/runtime.h>

#include <asmjit/x86.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <string>

namespace monad::compiler
{
    class VM : public evmc_vm
    {
    public:
        VM();

        evmc_result execute(
            evmc_host_interface const *host, evmc_host_context *context,
            evmc_revision rev, evmc_message const *msg, uint8_t const *code,
            size_t code_size);

        evmc_capabilities_flagset get_capabilities() const;

    private:
        asmjit::JitRuntime runtime_;
    };
}

extern "C" evmc_vm *evmc_create_monad_compiler_vm();
