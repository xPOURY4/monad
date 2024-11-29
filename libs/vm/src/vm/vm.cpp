#include <compiler/ir/x86.h>
#include <runtime/arithmetic.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>
#include <vm/vm.h>

#include <intx/intx.hpp>

#include <evmc/evmc.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <variant>

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

    constexpr evmc_result error_result(evmc_status_code code)
    {
        return evmc_result{
            .status_code = code,
            .gas_left = 0,
            .gas_refund = 0,
            .output_data = nullptr,
            .output_size = 0,
            .release = nullptr,
            .create_address = {},
            .padding = {},
        };
    }

    std::variant<std::span<std::uint8_t const>, evmc_status_code>
    copy_result_data(runtime::Context &ctx, runtime::Result const &res)
    {
        auto offset_word = intx::be::load<utils::uint256_t>(res.offset);
        auto size_word = intx::be::load<utils::uint256_t>(res.size);

        if (MONAD_COMPILER_UNLIKELY(
                offset_word > std::numeric_limits<std::uint32_t>::max())) {
            return EVMC_OUT_OF_MEMORY;
        }

        if (MONAD_COMPILER_UNLIKELY(
                size_word > std::numeric_limits<std::uint32_t>::max())) {
            return EVMC_OUT_OF_MEMORY;
        }

        auto offset = static_cast<std::uint32_t>(offset_word);
        auto size = static_cast<std::uint32_t>(size_word);

        ctx.expand_memory_unchecked(runtime::saturating_add(offset, size));
        if (MONAD_COMPILER_UNLIKELY(ctx.gas_remaining < 0)) {
            return EVMC_OUT_OF_GAS;
        }

        auto *output_buf = new std::uint8_t[size];
        std::copy_n(ctx.memory.begin() + offset, size, &output_buf[0]);
        return std::span{output_buf, size};
    }

    void release_result(evmc_result const *result)
    {
        MONAD_COMPILER_DEBUG_ASSERT(result);

        if (result->output_size > 0) {
            MONAD_COMPILER_DEBUG_ASSERT(result->output_data);
            delete[] result->output_data;
        }
    }

    evmc_result VM::execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size)
    {
        using enum runtime::StatusCode;

        auto contract_main = native::compile(runtime_, {code, code_size}, rev);
        if (!contract_main) {
            return error_result(EVMC_INTERNAL_ERROR);
        }

        auto ret = runtime::Result{};
        auto ctx = runtime::Context{
            .host = host,
            .context = context,
            .gas_remaining = msg->gas,
            .gas_refund = 0,
            .env =
                {
                    .evmc_flags = msg->flags,
                    .depth = msg->depth,
                    .recipient = msg->recipient,
                    .sender = msg->sender,
                    .value = msg->value,
                    .create2_salt = msg->create2_salt,
                    .input_data = {msg->input_data, msg->input_size},
                    .code = {code, code_size},
                    .return_data = {},
                },
            .memory = {},
            .memory_cost = 0,
        };

        auto *stack_ptr = reinterpret_cast<std::uint8_t *>(
            std::aligned_alloc(32, sizeof(utils::uint256_t) * 1024));

        (*contract_main)(&ret, &ctx, stack_ptr);

        std::free(stack_ptr);

        switch (ret.status) {
        case OutOfGas:
            return error_result(EVMC_OUT_OF_GAS);
        case StackOutOfBounds:
            return error_result(EVMC_STACK_OVERFLOW);
        case StaticModeViolation:
            return error_result(EVMC_STATIC_MODE_VIOLATION);
        case InvalidMemoryAccess:
            return error_result(EVMC_INVALID_MEMORY_ACCESS);
        default:
            break;
        }

        MONAD_COMPILER_DEBUG_ASSERT(
            ret.status == Success || ret.status == Revert);

        auto maybe_output = copy_result_data(ctx, ret);
        if (auto *ec = std::get_if<evmc_status_code>(&maybe_output)) {
            return error_result(*ec);
        }

        MONAD_COMPILER_DEBUG_ASSERT(
            std::holds_alternative<std::span<std::uint8_t const>>(
                maybe_output));

        auto output = std::get<std::span<std::uint8_t const>>(maybe_output);

        return evmc_result{
            .status_code = ret.status == Success ? EVMC_SUCCESS : EVMC_REVERT,
            .gas_left = ctx.gas_remaining,
            .gas_refund = ret.status == Success ? ctx.gas_refund : 0,
            .output_data = output.data(),
            .output_size = output.size(),
            .release = release_result,
            .create_address = {},
            .padding = {},
        };
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
