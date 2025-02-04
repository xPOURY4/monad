#pragma once

#include <monad/compiler/ir/x86.hpp>

#include <monad/runtime/runtime.hpp>

#include <asmjit/x86.h>

#include <intx/intx.hpp>

#include <string>

namespace monad::compiler
{
    class VM
    {
    public:
        /// Compile the given `code` for given `evmc_revision`. If `compile`
        /// succeeds, then it returns an entry point which can be called to
        /// execute the code.
        /// Remeber to call `release` when the entry point is not needed
        /// anymore, to release the resources consumed by the entry point. If
        /// `asm_log_file_path` is not null, then human readable x86 code
        /// is printed to this file, and runtime debug logging is enabled.
        std::optional<native::entrypoint_t> compile(
            evmc_revision, uint8_t const *code, size_t code_size,
            char const *asm_log_file_path = nullptr);

        /// Execute an entry point returned by `compile`.
        evmc_result execute(
            native::entrypoint_t contract_main, evmc_host_interface const *host,
            evmc_host_context *context, evmc_message const *msg,
            uint8_t const *code, size_t code_size);

        /// Release the resources consumed by the given entry point.
        void release(native::entrypoint_t);

        /// The composition of `compile`, `execute`, `release`.
        evmc_result compile_and_execute(
            evmc_host_interface const *host, evmc_host_context *context,
            evmc_revision rev, evmc_message const *msg, uint8_t const *code,
            size_t code_size, char const *asm_log_file_path = nullptr);

    private:
        asmjit::JitRuntime runtime_;
    };
}
