#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <cstdint>
#include <utility>

namespace monad::runtime
{
    std::uint32_t Context::get_memory_offset(utils::uint256_t offset)
    {
        constexpr auto max_offset = (1 << Context::max_memory_offset_bits) - 1;

        if (MONAD_COMPILER_UNLIKELY(offset > max_offset)) {
            exit(StatusCode::OutOfGas);
        }

        return static_cast<uint32_t>(offset);
    }

    std::pair<std::uint32_t, std::uint32_t> Context::get_memory_offset_and_size(
        utils::uint256_t offset, utils::uint256_t size)
    {
        if (size == 0) {
            return {0, 0};
        }

        return {get_memory_offset(offset), get_memory_offset(size)};
    }

    void Context::expand_memory(std::uint32_t size)
    {
        expand_memory_unchecked(size);

        if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
            exit(StatusCode::OutOfGas);
        }
    }

    void Context::expand_memory_unchecked(std::uint32_t size)
    {
        if (memory.size() < size) {
            auto memory_size_word = (size + 31) / 32;
            auto new_memory_cost = (memory_size_word * memory_size_word) / 512 +
                                   (3 * memory_size_word);

            auto expansion_cost = new_memory_cost - memory_cost;
            gas_remaining -= static_cast<std::int64_t>(expansion_cost);

            memory.resize(memory_size_word * 32);
            memory_cost = new_memory_cost;
        }
    }

    utils::uint256_t Context::mload(utils::uint256_t offset_word)
    {
        auto offset = get_memory_offset(offset_word);
        expand_memory(offset + 32);

        return intx::be::unsafe::load<utils::uint256_t>(memory.data() + offset);
    }

    void Context::mstore(utils::uint256_t offset_word, utils::uint256_t value)
    {
        auto offset = get_memory_offset(offset_word);
        expand_memory(offset + 32);

        set_memory_word(offset, value);
    }

    void Context::mstore8(utils::uint256_t offset_word, utils::uint256_t value)
    {
        auto offset = get_memory_offset(offset_word);
        expand_memory(offset + 1);

        set_memory_byte(offset, intx::as_bytes(value)[0]);
    }

    utils::uint256_t Context::msize() const
    {
        return memory.size();
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
