#include <runtime/exit.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <cstdint>

namespace monad::runtime
{
    namespace
    {
        std::uint32_t
        get_memory_offset(ExitContext *exit_ctx, utils::uint256_t offset)
        {
            constexpr auto max_offset =
                (1 << Context::max_memory_offset_bits) - 1;

            if (MONAD_COMPILER_UNLIKELY(offset > max_offset)) {
                runtime_exit(
                    exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
            }

            return static_cast<uint32_t>(offset);
        }
    }

    void Context::expand_memory(ExitContext *exit_ctx, std::uint32_t size)
    {
        if (memory.size() < size) {
            auto memory_size_word = (size + 31) / 32;
            auto new_memory_cost = (memory_size_word * memory_size_word) / 512 +
                                   (3 * memory_size_word);

            auto expansion_cost = new_memory_cost - memory_cost;
            gas_remaining -= static_cast<std::int64_t>(expansion_cost);

            if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
                runtime_exit(
                    exit_ctx->stack_pointer, exit_ctx->ctx, Error::OutOfGas);
            }

            memory.resize(memory_size_word * 32);
        }
    }

    utils::uint256_t
    Context::mload(ExitContext *exit_ctx, utils::uint256_t offset_word)
    {
        auto offset = get_memory_offset(exit_ctx, offset_word);
        expand_memory(exit_ctx, offset + 32);

        return intx::be::unsafe::load<utils::uint256_t>(memory.data() + offset);
    }

    void Context::mstore(
        ExitContext *exit_ctx, utils::uint256_t offset_word,
        utils::uint256_t value)
    {
        auto offset = get_memory_offset(exit_ctx, offset_word);
        expand_memory(exit_ctx, offset + 32);

        set_memory_word(offset, value);
    }

    void Context::set_memory_word(std::uint32_t offset, utils::uint256_t word)
    {
        MONAD_COMPILER_ASSERT(offset + 31 < memory.size());
        intx::be::unsafe::store(memory.data() + offset, word);
    }

    void Context::set_memory_byte(std::uint32_t offset, std::uint8_t byte)
    {
        MONAD_COMPILER_ASSERT(offset < memory.size());
        memory[offset] = byte;
    }
}
