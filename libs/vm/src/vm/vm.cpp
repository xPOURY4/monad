#include <evmc/evmc.hpp>

void destroy(evmc_vm *vm) {}

evmc_result execute(
    evmc_vm *vm, evmc_host_interface const *host, evmc_host_context *context,
    evmc_revision rev, evmc_message const *msg, uint8_t const *code,
    size_t code_size)
{
    return evmc_result{
        evmc_status_code::EVMC_FAILURE, 0, 0, nullptr, 0, nullptr};
}

evmc_capabilities_flagset get_capabilities(evmc_vm *vm)
{
    return EVMC_CAPABILITY_EVM1;
}

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
