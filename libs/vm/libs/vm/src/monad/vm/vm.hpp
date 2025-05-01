#pragma once

#include <monad/vm/code.hpp>
#include <monad/vm/compiler/ir/x86.hpp>
#include <monad/vm/runtime/allocator.hpp>

#include <asmjit/x86.h>

namespace monad::vm
{
    class VM
    {
        asmjit::JitRuntime runtime_;
        runtime::EvmStackAllocator stack_allocator_;
        runtime::EvmMemoryAllocator memory_allocator_;

    public:
        VM(std::size_t max_stack_cache_byte_size_ =
               runtime::EvmStackAllocator::DEFAULT_MAX_CACHE_BYTE_SIZE,
           std::size_t max_memory_cache_byte_size_ =
               runtime::EvmMemoryAllocator::DEFAULT_MAX_CACHE_BYTE_SIZE)
            : stack_allocator_{max_stack_cache_byte_size_}
            , memory_allocator_{max_memory_cache_byte_size_}
        {
        }

        /// Compile the given `code` for given `evmc_revision`.
        SharedNativecode compile(
            evmc_revision, uint8_t const *code, size_t code_size,
            compiler::native::CompilerConfig const & = {});

        /// Execute an entry point returned by `compile`.
        evmc_result execute(
            compiler::native::entrypoint_t contract_main,
            evmc_host_interface const *host, evmc_host_context *context,
            evmc_message const *msg, uint8_t const *code, size_t code_size);

        /// First `compile` then `execute`.
        evmc_result compile_and_execute(
            evmc_host_interface const *host, evmc_host_context *context,
            evmc_revision rev, evmc_message const *msg, uint8_t const *code,
            size_t code_size, compiler::native::CompilerConfig const & = {});

        [[gnu::always_inline]]
        runtime::EvmStackAllocator get_stack_allocator()
        {
            return stack_allocator_;
        }

        [[gnu::always_inline]]
        runtime::EvmMemoryAllocator get_memory_allocator()
        {
            return memory_allocator_;
        }
    };
}
