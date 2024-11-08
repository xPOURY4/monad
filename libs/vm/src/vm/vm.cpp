#include <compiler/compiler.h>
#include <runtime/runtime.h>
#include <vm/vm.h>

#include <evmc/evmc.h>

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
        (void)vm;
        (void)host;
        (void)rev;
        (void)msg;
        (void)context;
        (void)code;
        (void)code_size;

        return {};
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
    return new evmc_vm{
        EVMC_ABI_VERSION,
        "monad-compiler-vm",
        "0.0.0",
        ::destroy,
        ::execute,
        ::get_capabilities,
        nullptr};
}
