#include <monad/vm/compiler/ir/x86.hpp>
#include <monad/vm/compiler/ir/x86/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/vm.hpp>

#include <evmc/evmc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>

namespace monad::vm
{
    std::optional<compiler::native::entrypoint_t> VM::compile(
        evmc_revision rev, uint8_t const *code, size_t code_size,
        compiler::native::CompilerConfig const &config)
    {
        return compiler::native::compile(
            runtime_, {code, code_size}, rev, config);
    }

    evmc_result VM::execute(
        compiler::native::entrypoint_t contract_main,
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_message const *msg, uint8_t const *code, size_t code_size)
    {
        MONAD_VM_ASSERT(code_size <= std::numeric_limits<std::uint32_t>::max());
        MONAD_VM_ASSERT(
            msg->input_size <= std::numeric_limits<std::uint32_t>::max());

        auto ctx = runtime::Context::from(
            memory_allocator_, host, context, msg, {code, code_size});

        auto const stack_ptr = stack_allocator_.allocate();

        contract_main(&ctx, stack_ptr.get());

        return ctx.copy_to_evmc_result();
    }

    evmc_result VM::compile_and_execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size, compiler::native::CompilerConfig const &config)
    {
        if (auto f = compile(rev, code, code_size, config)) {
            auto r = execute(*f, host, context, msg, code, code_size);
            runtime_.release(*f);
            return r;
        }

        return runtime::evmc_error_result(EVMC_INTERNAL_ERROR);
    }

    void VM::release(compiler::native::entrypoint_t f)
    {
        runtime_.release(f);
    }
}
