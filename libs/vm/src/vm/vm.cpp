#include <runtime/runtime.h>
#include <vm/vm.h>

#include <compiler/ir/x86.h>

#include <evmc/evmc.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace
{

    void destroy(evmc_vm *vm)
    {
        reinterpret_cast<monad::compiler::VM *>(vm)->~VM();
    }

    evmc_result execute(
        evmc_vm *vm, evmc_host_interface const *host,
        evmc_host_context *context, evmc_revision rev, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        return reinterpret_cast<monad::compiler::VM *>(vm)->execute(
            host, context, rev, msg, code, code_size);
    }

    evmc_capabilities_flagset get_capabilities(evmc_vm *vm)
    {
        return reinterpret_cast<monad::compiler::VM *>(vm)->get_capabilities();
    }

}

namespace monad::compiler
{
    VM::VM()
        : evmc_vm{
              EVMC_ABI_VERSION,
              "monad-compiler-vm",
              "0.0.0",
              ::destroy,
              ::execute,
              ::get_capabilities,
              nullptr}
    {
    }

    evmc_result VM::execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size)
    {
        (void)msg;

        auto holder = asmjit::CodeHolder();
        holder.init(runtime_.environment(), runtime_.cpuFeatures());

        native::compile(holder, {code, code_size}, rev);

        native::entrypoint_t contract_main;
        runtime_.add(&contract_main, &holder);

        auto ret = runtime::Result{};
        auto ctx = runtime::Context{
            .host = host,
            .context = context,
        };

        contract_main(&ret, &ctx);

        return {};
    }

    evmc_capabilities_flagset VM::get_capabilities() const
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
    return new monad::compiler::VM();
}
