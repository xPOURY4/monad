#include <compiler/compiler.h>
#include <runtime/runtime.h>
#include <vm/execute_jit.h>
#include <vm/vm.h>

#include <evmc/evmc.h>

#include <llvm/Support/TargetSelect.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace
{

    void destroy(evmc_vm *) {}

    evmc_result execute(
        evmc_vm *vm, evmc_host_interface const *host,
        evmc_host_context *context, evmc_revision rev, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        // This function is currently a shortest-path implementation to taking
        // an `llvm::Module` and executing it via a JIT mechanism. It therefore
        // has a few important issues and limitations:
        //   - It uses the legacy `ExecutionEngine` API rather than ORCv2
        //   - The engine is rebuilt for every contract execution
        //   - Contracts will be recompiled every time they're executed

        (void)vm;
        (void)host;
        (void)rev;
        (void)msg;
        (void)context;

        auto [mod, entrypoint] =
            monad::compiler::compile_evm_bytecode(code, code_size);
        assert(mod && "Failed to compile bytecode");

        auto engine = monad::vm::create_engine(std::move(mod));
        std::cout << engine->getErrorMessage() << '\n';

        engine->finalizeObject();
        auto jit_entry_fn =
            reinterpret_cast<void (*)(monad_runtime_interface *)>(
                engine->getPointerToFunction(entrypoint));

        auto result = evmc_result{
            evmc_status_code::EVMC_FAILURE, 0, 0, nullptr, 0, nullptr, {}, {}};

        auto interface = monad_runtime_interface{
            .result = result,
            .host = host,
            .context = context,
            .revision = rev,
            .message = msg,
        };

        jit_entry_fn(&interface);

        return interface.result;
    }

    evmc_capabilities_flagset get_capabilities(evmc_vm *)
    {
        return EVMC_CAPABILITY_EVM1;
    }

}

/**
 * This function is a special entrypoint recognised by EVMC-compatible host
 * implementations. When a host loads `libmonad-compiler-vm.so` as a VM library,
 * it demangles the name to produce `evmc_create_monad_compiler_vm`, then loads
 * this function to construct the VM.
 */
extern "C" evmc_vm *evmc_create_monad_compiler_vm()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    monad::vm::bind_runtime();

    return new evmc_vm{
        EVMC_ABI_VERSION,
        "monad-compiler-vm",
        "0.0.0",
        ::destroy,
        ::execute,
        ::get_capabilities,
        nullptr};
}
