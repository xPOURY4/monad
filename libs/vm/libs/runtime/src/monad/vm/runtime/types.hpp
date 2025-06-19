#pragma once

#include <monad/vm/core/assert.h>
#include <monad/vm/runtime/allocator.hpp>
#include <monad/vm/runtime/bin.hpp>
#include <monad/vm/runtime/transmute.hpp>
#include <monad/vm/runtime/uint256.hpp>

#include <evmc/evmc.hpp>

#include <type_traits>
#include <variant>
#include <vector>

namespace monad::vm::runtime
{
    enum class StatusCode : uint64_t
    {
        Success = 0,
        Revert,
        Error,
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

        evmc_tx_context tx_context;

        ~Environment()
        {
            std::free(const_cast<uint8_t *>(return_data));
        }

        [[gnu::always_inline]]
        void set_return_data(
            std::uint8_t const *output_data, std::size_t output_size)
        {
            MONAD_VM_DEBUG_ASSERT(return_data_size == 0);
            return_data = output_data;
            return_data_size = output_size;
        }

        [[gnu::always_inline]]
        void clear_return_data()
        {
            std::free(const_cast<uint8_t *>(return_data));
            return_data = nullptr;
            return_data_size = 0;
        }
    };

    struct Memory
    {
        EvmMemoryAllocator allocator_;
        std::uint32_t size;
        std::uint32_t capacity;
        std::uint8_t *data;
        std::int64_t cost;

        static constexpr auto initial_capacity = 4096;

        static constexpr auto offset_bits = 28;

        using Offset = Bin<offset_bits>;

        Memory() = delete;

        explicit Memory(EvmMemoryAllocator allocator)
            : allocator_{allocator}
            , size{}
            , capacity{initial_capacity}
            , data{allocator_.aligned_alloc_cached()}
            , cost{}
        {
            memset(data, 0, capacity);
        }

        Memory(Memory &&m)
            : allocator_{m.allocator_}
            , size{m.size}
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
        void dealloc(uint8_t *d)
        {
            if (capacity == initial_capacity) {
                allocator_.free_cached(d);
            }
            else {
                std::free(d);
            }
        }
    };

    struct Context
    {
        static Context from(
            EvmMemoryAllocator mem_alloc, evmc_host_interface const *host,
            evmc_host_context *context, evmc_message const *msg,
            std::span<std::uint8_t const> code) noexcept;

        static Context empty() noexcept;

        evmc_host_interface const *host;
        evmc_host_context *context;

        std::int64_t gas_remaining;
        std::int64_t gas_refund;

        Environment env;

        Result result = {};

        Memory memory;

        void *exit_stack_ptr = nullptr;

        [[gnu::always_inline]]
        constexpr void deduct_gas(std::int64_t const gas) noexcept
        {
            gas_remaining -= gas;
            if (MONAD_VM_UNLIKELY(gas_remaining < 0)) {
                exit(StatusCode::Error);
            }
        }

        [[gnu::always_inline]]
        constexpr void deduct_gas(Bin<32> const gas) noexcept
        {
            return deduct_gas(*gas);
        }

        [[gnu::always_inline]]
        static constexpr int64_t
        memory_cost_from_word_count(Bin<32> const word_count) noexcept
        {
            auto c = static_cast<uint64_t>(*word_count);
            return static_cast<int64_t>((c * c) / 512 + (3 * c));
        }

        void increase_capacity(uint32_t old_size, Bin<31> new_size);

        void expand_memory(Bin<30> min_size)
        {
            if (memory.size < *min_size) {
                auto wsize = shr_ceil<5>(min_size);
                std::int64_t const new_cost =
                    memory_cost_from_word_count(wsize);
                Bin<31> const new_size = shl<5>(wsize);
                MONAD_VM_DEBUG_ASSERT(new_cost >= memory.cost);
                std::int64_t const expansion_cost = new_cost - memory.cost;
                // Gas check before increasing capacity:
                deduct_gas(expansion_cost);
                uint32_t const old_size = memory.size;
                memory.size = *new_size;
                memory.cost = new_cost;
                if (memory.capacity < *new_size) {
                    increase_capacity(old_size, new_size);
                }
            }
        }

        [[gnu::always_inline]]
        Memory::Offset get_memory_offset(uint256_t const &offset)
        {
            if (MONAD_VM_UNLIKELY(
                    !is_bounded_by_bits<Memory::offset_bits>(offset))) {
                exit(StatusCode::Error);
            }
            return Memory::Offset::unsafe_from(static_cast<uint32_t>(offset));
        }

        void exit [[noreturn]] (StatusCode code) noexcept;

        evmc::Result copy_to_evmc_result();

    private:
        std::variant<std::span<std::uint8_t const>, evmc_status_code>
        copy_result_data();
    };

    // Update context.S accordingly if these offsets change:
    static_assert(offsetof(Context, gas_remaining) == 16);
    static_assert(offsetof(Context, memory) == 512);
    static_assert(offsetof(Memory, size) == 8);
    static_assert(offsetof(Memory, capacity) == 12);
    static_assert(offsetof(Memory, cost) == 24);
}

extern "C" void monad_vm_runtime_increase_capacity(
    monad::vm::runtime::Context *, uint32_t old_size,
    monad::vm::runtime::Bin<31> new_size);
