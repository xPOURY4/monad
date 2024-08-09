#include <vm/execute_jit.h>

#include <compiler/compiler.h>

#include <evmc/evmc.hpp>

#include <llvm/Support/TargetSelect.h>

namespace
{

    void destroy(evmc_vm *vm)
    {
        (void)vm;
    }

    evmc_result execute(
        evmc_vm *vm, evmc_host_interface const *host,
        evmc_host_context *context, evmc_revision rev, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        (void)vm;
        (void)host;
        (void)rev;
        (void)msg;

        auto [mod, entrypoint] =
            monad::compiler::compile_evm_bytecode(code, code_size);
        assert(mod && "Failed to compile bytecode");

        auto engine = monad::vm::create_engine(std::move(mod));
        assert(engine.has_value() && "Failed to create execution engine");

        engine.value()->finalizeObject();
        auto jit_entry_fn =
            reinterpret_cast<void (*)(evmc_result *, evmc_host_context *)>(
                engine.value()->getPointerToFunction(entrypoint));

        auto result = evmc_result{
            evmc_status_code::EVMC_FAILURE, 0, 0, nullptr, 0, nullptr};

        jit_entry_fn(&result, context);

        return result;
    }

    evmc_capabilities_flagset get_capabilities(evmc_vm *vm)
    {
        (void)vm;
        return EVMC_CAPABILITY_EVM1;
    }

}

extern "C" evmc_vm *evmc_create_monad_compiler_vm()
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    return new evmc_vm{
        EVMC_ABI_VERSION,
        "monad-compiler-vm",
        "0.0.0",
        ::destroy,
        ::execute,
        ::get_capabilities,
        nullptr};
}
