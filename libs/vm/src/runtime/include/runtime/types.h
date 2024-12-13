#pragma once

#include <utils/assert.h>
#include <utils/uint256.h>

#include <evmc/evmc.hpp>

#include <type_traits>
#include <vector>

namespace monad::runtime
{
    enum class StatusCode : uint64_t
    {
        Success = 0,
        Revert,
        OutOfGas,
        StackOutOfBounds,
        StaticModeViolation,
        InvalidMemoryAccess,
        InvalidInstruction,
    };

    struct alignas(uint64_t) Result
    {
        uint8_t offset[32];
        uint8_t size[32];
        StatusCode status;
    };

    struct Environment
    {
        std::uint32_t evmc_flags;
        std::int32_t depth;
        evmc::address recipient;
        evmc::address sender;
        evmc::bytes32 value;
        evmc::bytes32 create2_salt;

        std::uint8_t const *input_data;
        std::uint8_t const *code;
        std::uint8_t const *return_data;

        std::uint32_t input_data_size;
        std::uint32_t code_size;
        std::size_t return_data_size;

        [[gnu::always_inline]]
        void set_return_data(
            std::uint8_t const *output_data, std::size_t output_size)
        {
            MONAD_COMPILER_DEBUG_ASSERT(return_data_size == 0);
            return_data = output_data;
            return_data_size = output_size;
        }

        [[gnu::always_inline]]
        void clear_return_data()
        {
            return_data_size = 0;
        }
    };

    struct Memory
    {
        std::uint32_t size;
        std::uint32_t capacity;
        std::uint8_t *data;
        std::int64_t cost;

        static constexpr auto initial_capacity = 4096;

        Memory()
            : size{}
            , capacity{initial_capacity}
            , data{alloc(capacity)}
            , cost{}
        {
            memset(data, 0, capacity);
        }

        Memory(Memory &&m)
            : size{m.size}
            , capacity{m.capacity}
            , data{m.data}
            , cost{m.cost}
        {
            m.clear();
        }

        Memory &operator=(Memory &&m)
        {
            dealloc(data);

            size = m.size;
            capacity = m.capacity;
            data = m.data;
            cost = m.cost;

            m.clear();
            return *this;
        }

        Memory(Memory const &) = delete;

        Memory &operator=(Memory const &) = delete;

        ~Memory()
        {
            dealloc(data);
        }

        [[gnu::always_inline]]
        void clear()
        {
            size = 0;
            capacity = 0;
            data = nullptr;
            cost = 0;
        }

        [[gnu::always_inline]]
        static uint8_t *alloc(std::uint32_t n)
        {
            return static_cast<uint8_t *>(std::aligned_alloc(32, n));
        }

        [[gnu::always_inline]]
        static void dealloc(uint8_t *d)
        {
            std::free(d);
        }
    };

    struct Context
    {
        static constexpr std::size_t max_memory_offset_bits = 24;
        // Make sure that `max_memory_offset` is sufficiently small,
        // so that `a + b` does not overflow `std::uint32_t` for
        // `a <= max_memory_offset` and `b <= max_memory_offset`.
        static constexpr std::size_t max_memory_offset =
            (1 << max_memory_offset_bits) - 1;

        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;

        Result result = {};

        Memory memory = {};

        void *exit_stack_ptr = nullptr;

        [[gnu::always_inline]]
        void deduct_gas(std::int64_t gas)
        {
            gas_remaining -= gas;
            if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
                exit(StatusCode::OutOfGas);
            }
        }

        [[gnu::always_inline]]
        static int64_t memory_cost_from_word_count(std::uint32_t word_count)
        {
            auto c = static_cast<int64_t>(word_count);
            return (c * c) / 512 + (3 * c);
        }

        void expand_memory(std::uint32_t min_size)
        {
            if (memory.size < min_size) {
                auto wsize = (min_size + 31) / 32;
                auto new_size = wsize * 32;
                auto new_cost = memory_cost_from_word_count(wsize);
                auto expansion_cost = new_cost - memory.cost;
                // Must perform gas check before expanding:
                deduct_gas(expansion_cost);
                if (memory.capacity < new_size) {
                    memory.capacity = std::max(memory.capacity * 2, new_size);
                    MONAD_COMPILER_DEBUG_ASSERT((memory.capacity & 31) == 0);
                    std::uint8_t *new_data = Memory::alloc(memory.capacity);
                    std::memcpy(new_data, memory.data, memory.size);
                    std::memset(
                        new_data + memory.size,
                        0,
                        memory.capacity - memory.size);
                    Memory::dealloc(memory.data);
                    memory.data = new_data;
                }
                memory.size = new_size;
                memory.cost = new_cost;
            }
        }

        [[gnu::always_inline]]
        std::uint32_t get_memory_offset(utils::uint256_t const &offset)
        {
            if (MONAD_COMPILER_UNLIKELY(offset > max_memory_offset)) {
                exit(StatusCode::OutOfGas);
            }
            return static_cast<uint32_t>(offset);
        }

        [[gnu::always_inline]]
        evmc_tx_context get_tx_context() const
        {
            return host->get_tx_context(context);
        }

        void exit [[noreturn]] (StatusCode code) noexcept;
    };
}
