#pragma once

#include <runtime/runtime.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <string>

extern "C" evmc_vm *evmc_create_monad_compiler_vm();

namespace monad::vm
{
    void bind_runtime();
}

namespace monad::vm::testing
{
    /**
     * Wrapper class for simple computational executions of the EVM that don't
     * require host context interaction.
     *
     * It is likely that details of this class will be changed or even removed
     * completely in the future; its main purpose is to boostrap our compiler
     * implementation to the point of running actual EVM host tests by ensuring
     * that internal implementation details are handled correctly in a first
     * version.
     */
    class standalone_evm_jit
    {
        static_assert(
            sizeof(::intx::uint256) == sizeof(uint64_t[4]),
            "Unexpected layout for uint256 type");

    public:
        /**
         * JIT-compile a hex-encoded program to native code and bind the
         * resulting symbols to this object's members.
         */
        standalone_evm_jit(std::string const &program);

        ~standalone_evm_jit();
        standalone_evm_jit(standalone_evm_jit const &) = delete;
        standalone_evm_jit &operator=(standalone_evm_jit const &) = delete;
        standalone_evm_jit(standalone_evm_jit &&);
        standalone_evm_jit &operator=(standalone_evm_jit &&);

        /**
         * Get the current value of the EVM stack pointer.
         */
        uint16_t stack_pointer() const;

        /**
         * Get the EVM word at a particular stack offset.
         *
         * Asserts if the given index is out of bounds with respect to the
         * current value of the stack pointer.
         */
        ::intx::uint256 stack(std::size_t) const;

        /**
         * Call the JIT-compiled contract code.
         *
         * This method does not expose the host and result pointers; programs
         * that interact with the host will therefore behave incorrectly when
         * executed via this entrypoint.
         */
        void operator()() const;

    private:
        void (*entry_point_)(monad_runtime_interface *);
        uint16_t *stack_pointer_;
        ::intx::uint256 *stack_;

        // We need to keep the execution engine alive for as long as the
        // containing object is, but because of #19 we can't directly expose it
        // as an ExecutionEngine to the test code using this class. The
        // destructor for this class is therefore responsible for casting this
        // pointer back to an ExecutionEngine and destroying it appropriately.
        //
        // https://github.com/monad-crypto/monad-compiler/issues/19
        void *llvm_execution_engine_;

        void destroy_engine();
        void reset();
    };
}
