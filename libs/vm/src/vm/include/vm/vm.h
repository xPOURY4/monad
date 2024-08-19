#pragma once

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <string>

extern "C" evmc_vm *evmc_create_monad_compiler_vm();

namespace monad::vm
{
    struct jit_result
    {
        static_assert(sizeof(::intx::uint256) == sizeof(uint64_t[4]));

        void (*entry_point)(evmc_result *, evmc_host_context *);
        uint16_t *stack_pointer;
        ::intx::uint256 *stack;
    };

    jit_result jit_compile_program(std::string const &in);
}
