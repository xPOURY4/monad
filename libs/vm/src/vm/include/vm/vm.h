#pragma once

#include <compiler/ir/x86.h>

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

        std::optional<native::entrypoint_t> compile(
            evmc_revision, uint8_t const *code, size_t code_size,
            char const *asm_log);

        evmc_result execute(
            native::entrypoint_t contract_main, evmc_host_interface const *host,
            evmc_host_context *context, evmc_message const *msg,
            uint8_t const *code, size_t code_size);

        evmc_result compile_and_execute(
            evmc_host_interface const *host, evmc_host_context *context,
            evmc_revision rev, evmc_message const *msg, uint8_t const *code,
            size_t code_size);

        evmc_capabilities_flagset get_capabilities() const;

    private:
        asmjit::JitRuntime runtime_;
    };
}

extern "C" void *monad_compiler_compile(
    evmc_vm *, evmc_revision, uint8_t const *code, size_t code_size,
    char const *asm_log);

extern "C" evmc_result monad_compiler_execute(
    evmc_vm *, void *contract_main, evmc_host_interface const *host,
    evmc_host_context *context, evmc_message const *msg, uint8_t const *code,
    size_t code_size);

extern "C" evmc_vm *evmc_create_monad_compiler_vm();
