#include <monad/runtime/transmute.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/cases.hpp>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <variant>

namespace monad::runtime
{
    namespace
    {
        void release_result(evmc_result const *result)
        {
            MONAD_COMPILER_DEBUG_ASSERT(result);
            std::free(const_cast<std::uint8_t *>(result->output_data));
        }
    }

    Context Context::from(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_message const *msg, std::span<std::uint8_t const> code) noexcept
    {
        return Context{
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
                    .code = code.data(),
                    .return_data = {},
                    .input_data_size =
                        static_cast<std::uint32_t>(msg->input_size),
                    .code_size = static_cast<std::uint32_t>(code.size()),
                    .return_data_size = 0,
                    .tx_context = host->get_tx_context(context),
                },
            .result = {},
            .memory = {},
        };
    }

    std::variant<std::span<std::uint8_t const>, evmc_status_code>
    Context::copy_result_data()
    {
        if (gas_remaining < 0) {
            return EVMC_OUT_OF_GAS;
        }

        auto const size_word = std::bit_cast<utils::uint256_t>(result.size);
        if (!runtime::is_bounded_by_bits<monad::runtime::Memory::offset_bits>(
                size_word)) {
            return EVMC_OUT_OF_GAS;
        }

        auto size = runtime::Memory::Offset::unsafe_from(
            static_cast<uint32_t>(size_word));
        if (*size == 0) {
            return std::span<std::uint8_t const>({});
        }

        auto offset_word = std::bit_cast<utils::uint256_t>(result.offset);
        if (!runtime::is_bounded_by_bits<monad::runtime::Memory::offset_bits>(
                offset_word)) {
            return EVMC_OUT_OF_GAS;
        }

        auto offset = runtime::Memory::Offset::unsafe_from(
            static_cast<uint32_t>(offset_word));

        auto memory_end = offset + size;
        auto *output_buf = reinterpret_cast<std::uint8_t *>(std::malloc(*size));

        if (*memory_end <= memory.size) {
            std::memcpy(output_buf, memory.data + *offset, *size);
        }
        else {
            auto memory_cost = runtime::Context::memory_cost_from_word_count(
                shr_ceil<5>(memory_end));
            gas_remaining -= memory_cost - memory.cost;

            if (gas_remaining < 0) {
                std::free(output_buf);
                return EVMC_OUT_OF_GAS;
            }

            if (*offset < memory.size) {
                auto n = memory.size - *offset;
                std::memcpy(output_buf, memory.data + *offset, n);
                std::memset(output_buf + n, 0, *memory_end - memory.size);
            }
            else {
                std::memset(output_buf, 0, *size);
            }
        }

        return std::span{output_buf, *size};
    }

    evmc_result Context::copy_to_evmc_result()
    {
        using enum runtime::StatusCode;

        if (result.status == Error) {
            return evmc_error_result(EVMC_FAILURE);
        }

        MONAD_COMPILER_DEBUG_ASSERT(
            result.status == Success || result.status == Revert);

        return std::visit(
            utils::Cases{
                [](evmc_status_code ec) { return evmc_error_result(ec); },
                [this](std::span<std::uint8_t const> output) {
                    return evmc_result{
                        .status_code = result.status == Success ? EVMC_SUCCESS
                                                                : EVMC_REVERT,
                        .gas_left = gas_remaining,
                        .gas_refund = result.status == Success ? gas_refund : 0,
                        .output_data = output.data(),
                        .output_size = output.size(),
                        .release = release_result,
                        .create_address = {},
                        .padding = {},
                    };
                }},
            copy_result_data());
    }
}
