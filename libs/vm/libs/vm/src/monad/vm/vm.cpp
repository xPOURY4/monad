#include <monad/compiler/ir/x86.hpp>
#include <monad/compiler/ir/x86/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/uint256.hpp>
#include <monad/vm/vm.hpp>

#include <evmc/evmc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace monad::compiler
{
    std::optional<native::entrypoint_t> VM::compile(
        evmc_revision rev, uint8_t const *code, size_t code_size,
        native::CompilerConfig const &config)
    {
        return native::compile(runtime_, {code, code_size}, rev, config);
    }

    evmc_result VM::execute(
        native::entrypoint_t contract_main, evmc_host_interface const *host,
        evmc_host_context *context, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        MONAD_VM_ASSERT(code_size <= std::numeric_limits<std::uint32_t>::max());
        MONAD_VM_ASSERT(
            msg->input_size <= std::numeric_limits<std::uint32_t>::max());

        auto ctx =
            vm::runtime::Context::from(host, context, msg, {code, code_size});

        auto *stack_ptr = reinterpret_cast<std::uint8_t *>(
            std::aligned_alloc(32, sizeof(vm::utils::uint256_t) * 1024));

        contract_main(&ctx, stack_ptr);

        std::free(stack_ptr);

        return ctx.copy_to_evmc_result();
    }

    evmc_result VM::compile_and_execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size, native::CompilerConfig const &config)
    {
        if (auto f = compile(rev, code, code_size, config)) {
            auto r = execute(*f, host, context, msg, code, code_size);
            runtime_.release(*f);
            return r;
        }

        return vm::runtime::evmc_error_result(EVMC_INTERNAL_ERROR);
    }

    void VM::release(native::entrypoint_t f)
    {
        runtime_.release(f);
    }
}
