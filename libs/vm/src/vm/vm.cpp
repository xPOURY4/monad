#include <bit>
#include <compiler/ir/x86.h>
#include <cstring>
#include <optional>
#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>
#include <vm/vm.h>

#include <evmc/evmc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <variant>

namespace
{
    using namespace monad;
    using namespace monad::compiler;

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
    copy_result_data(runtime::Context &ctx)
    {
        if (ctx.gas_remaining < 0) {
            return EVMC_OUT_OF_GAS;
        }
        auto size_word = std::bit_cast<utils::uint256_t>(ctx.result.size);
        if (!runtime::is_bounded_by_bits<runtime::Memory::offset_bits>(
                size_word)) {
            return EVMC_OUT_OF_GAS;
        }
        auto size = runtime::Memory::Offset::unsafe_from(
            static_cast<uint32_t>(size_word));
        if (*size == 0) {
            return std::span<std::uint8_t const>({});
        }
        auto offset_word = std::bit_cast<utils::uint256_t>(ctx.result.offset);
        if (!runtime::is_bounded_by_bits<runtime::Memory::offset_bits>(
                offset_word)) {
            return EVMC_OUT_OF_GAS;
        }
        auto offset = runtime::Memory::Offset::unsafe_from(
            static_cast<uint32_t>(offset_word));

        auto memory_end = offset + size;
        auto *output_buf = reinterpret_cast<std::uint8_t *>(std::malloc(*size));
        if (*memory_end <= ctx.memory.size) {
            std::memcpy(output_buf, ctx.memory.data + *offset, *size);
        }
        else {
            auto memory_cost = runtime::Context::memory_cost_from_word_count(
                shr_ceil<5>(memory_end));
            ctx.gas_remaining -= memory_cost - ctx.memory.cost;
            if (ctx.gas_remaining < 0) {
                std::free(output_buf);
                return EVMC_OUT_OF_GAS;
            }
            if (*offset < ctx.memory.size) {
                auto n = ctx.memory.size - *offset;
                std::memcpy(output_buf, ctx.memory.data + *offset, n);
                std::memset(output_buf + n, 0, *memory_end - ctx.memory.size);
            }
            else {
                std::memset(output_buf, 0, *size);
            }
        }
        return std::span{output_buf, *size};
    }

    void release_result(evmc_result const *result)
    {
        MONAD_COMPILER_DEBUG_ASSERT(result);
        std::free(const_cast<std::uint8_t *>(result->output_data));
    }
}

namespace monad::compiler
{
    std::optional<native::entrypoint_t> VM::compile(
        evmc_revision rev, uint8_t const *code, size_t code_size,
        char const *asm_log)
    {
        return native::compile(runtime_, {code, code_size}, rev, asm_log);
    }

    evmc_result VM::execute(
        native::entrypoint_t contract_main, evmc_host_interface const *host,
        evmc_host_context *context, evmc_message const *msg,
        uint8_t const *code, size_t code_size)
    {
        using enum runtime::StatusCode;

        MONAD_COMPILER_ASSERT(
            code_size <= std::numeric_limits<std::uint32_t>::max());
        MONAD_COMPILER_ASSERT(
            msg->input_size <= std::numeric_limits<std::uint32_t>::max());

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
                    .input_data = msg->input_data,
                    .code = code,
                    .return_data = {},
                    .input_data_size =
                        static_cast<std::uint32_t>(msg->input_size),
                    .code_size = static_cast<std::uint32_t>(code_size),
                    .return_data_size = 0,
                    .tx_context = host->get_tx_context(context),
                },
            .result = {},
            .memory = {},
        };

        auto *stack_ptr = reinterpret_cast<std::uint8_t *>(
            std::aligned_alloc(32, sizeof(utils::uint256_t) * 1024));

        contract_main(&ctx, stack_ptr);

        std::free(stack_ptr);

        switch (ctx.result.status) {
        case OutOfGas:
            return error_result(EVMC_OUT_OF_GAS);
        case StackOverflow:
            return error_result(EVMC_STACK_OVERFLOW);
        case StackUnderflow:
            return error_result(EVMC_STACK_UNDERFLOW);
        case BadJumpDest:
            return error_result(EVMC_BAD_JUMP_DESTINATION);
        case StaticModeViolation:
            return error_result(EVMC_STATIC_MODE_VIOLATION);
        case InvalidMemoryAccess:
            return error_result(EVMC_INVALID_MEMORY_ACCESS);
        case InvalidInstruction:
            return error_result(EVMC_UNDEFINED_INSTRUCTION);
        case Success:
            break;
        case Revert:
            break;
        }

        auto maybe_output = copy_result_data(ctx);
        if (auto *ec = std::get_if<evmc_status_code>(&maybe_output)) {
            return error_result(*ec);
        }

        MONAD_COMPILER_DEBUG_ASSERT(
            std::holds_alternative<std::span<std::uint8_t const>>(
                maybe_output));

        auto output = std::get<std::span<std::uint8_t const>>(maybe_output);

        return evmc_result{
            .status_code =
                ctx.result.status == Success ? EVMC_SUCCESS : EVMC_REVERT,
            .gas_left = ctx.gas_remaining,
            .gas_refund = ctx.result.status == Success ? ctx.gas_refund : 0,
            .output_data = output.data(),
            .output_size = output.size(),
            .release = release_result,
            .create_address = {},
            .padding = {},
        };
    }

    evmc_result VM::compile_and_execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg, uint8_t const *code,
        size_t code_size)
    {
        auto f = compile(rev, code, code_size, nullptr);
        if (!f) {
            return error_result(EVMC_INTERNAL_ERROR);
        }
        return execute(*f, host, context, msg, code, code_size);
    }
}

extern "C" void *monad_compiler_compile_debug(
    evmc_vm *vm, evmc_revision rev, uint8_t const *code, size_t code_size,
    char const *asm_log)
{
    auto contract_main = reinterpret_cast<monad::compiler::VM *>(vm)->compile(
        rev, code, code_size, asm_log);
    return contract_main ? reinterpret_cast<void *>(*contract_main) : nullptr;
}

extern "C" void *monad_compiler_compile(
    evmc_vm *vm, evmc_revision rev, uint8_t const *code, size_t code_size)
{
    return monad_compiler_compile_debug(vm, rev, code, code_size, nullptr);
}

extern "C" evmc_result monad_compiler_execute(
    evmc_vm *vm, void *contract_main, evmc_host_interface const *host,
    evmc_host_context *context, evmc_message const *msg, uint8_t const *code,
    size_t code_size)
{
    return reinterpret_cast<monad::compiler::VM *>(vm)->execute(
        reinterpret_cast<native::entrypoint_t>(contract_main),
        host,
        context,
        msg,
        code,
        code_size);
}
