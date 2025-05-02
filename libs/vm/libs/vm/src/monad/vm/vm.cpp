#include <monad/vm/code.hpp>
#include <monad/vm/compiler/ir/x86.hpp>
#include <monad/vm/compiler/ir/x86/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/interpreter/execute.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/vm.hpp>

#include <cstddef>
#include <cstdint>
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <span>

namespace monad::vm
{
    VM::VM(std::size_t max_stack_cache, std::size_t max_memory_cache)
        : stack_allocator_{max_stack_cache}
        , memory_allocator_{max_memory_cache}
    {
    }

    evmc::Result VM::execute(
        evmc_revision rev, evmc_host_interface const *host,
        evmc_host_context *context, evmc_message const *msg,
        evmc::bytes32 const &code_hash, SharedVarcode const &vcode)
    {
        auto const &icode = vcode->intercode();
        auto const &ncode = vcode->nativecode();
        if (ncode != nullptr && ncode->revision() == rev) {
            auto const entry = ncode->entrypoint();
            if (MONAD_VM_UNLIKELY(entry == nullptr)) {
                // Compilation has failed in this revision, so just execute
                // with interpreter.
                return execute_intercode(rev, host, context, msg, icode);
            }
            else {
                return execute_native_entrypoint(
                    host, context, msg, icode, entry);
            }
        }
        // Contract has not been attempted compiled yet in this revision, so
        // execute with interpreter. We will start async compile job when the
        // execution gas spent by interpreter becomes sufficiently high.
        auto result = execute_intercode(rev, host, context, msg, icode);
        auto const bound = compiler::native::max_code_size(
            compiler_config_.max_code_size_offset, icode->code_size());
        MONAD_VM_DEBUG_ASSERT(result.gas_left >= 0);
        MONAD_VM_DEBUG_ASSERT(msg->gas >= result.gas_left);
        uint64_t const gas_used =
            static_cast<uint64_t>(msg->gas - result.gas_left);
        // Note that execution gas is counted for the second time via the
        // intercode_gas_used function if this is a re-execution.
        if (vcode->intercode_gas_used(gas_used) >= bound) {
            compiler_.async_compile(rev, code_hash, icode, compiler_config_);
        }
        return result;
    }

    evmc::Result VM::execute_raw(
        evmc_revision rev, evmc_host_interface const *host,
        evmc_host_context *context, evmc_message const *msg,
        std::span<uint8_t const> code)
    {
        auto const stack_ptr = stack_allocator_.allocate();
        auto ctx = runtime::Context::from(
            memory_allocator_, host, context, msg, {code.data(), code.size()});

        interpreter::execute(rev, ctx, Intercode{code}, stack_ptr.get());

        return ctx.copy_to_evmc_result();
    }

    evmc::Result VM::execute_intercode(
        evmc_revision rev, evmc_host_interface const *host,
        evmc_host_context *context, evmc_message const *msg,
        SharedIntercode const &icode)
    {
        uint8_t const *const code = icode->code();
        size_t const code_size = icode->code_size();
        auto const stack_ptr = stack_allocator_.allocate();
        auto ctx = runtime::Context::from(
            memory_allocator_, host, context, msg, {code, code_size});

        interpreter::execute(rev, ctx, *icode, stack_ptr.get());

        return ctx.copy_to_evmc_result();
    }

    evmc::Result VM::execute_native_entrypoint(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_message const *msg, SharedIntercode const &icode,
        compiler::native::entrypoint_t entry)
    {
        uint8_t const *const code = icode->code();
        size_t const code_size = icode->code_size();
        auto ctx = runtime::Context::from(
            memory_allocator_, host, context, msg, {code, code_size});

        auto const stack_ptr = stack_allocator_.allocate();

        entry(&ctx, stack_ptr.get());

        return ctx.copy_to_evmc_result();
    }
}
