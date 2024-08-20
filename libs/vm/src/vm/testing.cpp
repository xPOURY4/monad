#include <compiler/compiler.h>
#include <utils/load_program.h>
#include <vm/execute_jit.h>
#include <vm/vm.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace monad::vm::testing
{
    standalone_evm_jit::standalone_evm_jit(std::string const &program)
    {
        auto bytes = utils::parse_hex_program(program);

        auto [mod, entrypoint] =
            monad::compiler::compile_evm_bytecode(bytes.data(), bytes.size());
        assert(mod && "Failed to compile bytecode");

        auto engine = monad::vm::create_engine(std::move(mod));
        if (engine->hasError()) {
            throw std::runtime_error(engine->getErrorMessage());
        }

        entry_point_ =
            reinterpret_cast<void (*)(evmc_result *, evmc_host_context *)>(
                engine->getPointerToFunction(entrypoint));
        assert(entry_point_ && "Failed to get pointer to entrypoint");

        stack_pointer_ = reinterpret_cast<uint16_t *>(
            engine->getGlobalValueAddress(compiler::constants::stack_pointer));
        assert(stack_pointer_ && "Failed to get pointer to stack pointer");

        stack_ = reinterpret_cast<::intx::uint256 *>(
            engine->getGlobalValueAddress(compiler::constants::stack));
        assert(stack_ && "Failed to get pointer to stack");

        engine->finalizeObject();
        llvm_execution_engine_ = static_cast<void *>(engine.release());
    }

    standalone_evm_jit::~standalone_evm_jit()
    {
        destroy_engine();
    }

    standalone_evm_jit::standalone_evm_jit(standalone_evm_jit &&other)
    {
        destroy_engine();

        entry_point_ = other.entry_point_;
        stack_pointer_ = other.stack_pointer_;
        stack_ = other.stack_;
        llvm_execution_engine_ = other.llvm_execution_engine_;

        other.reset();
    }

    standalone_evm_jit &
    standalone_evm_jit::operator=(standalone_evm_jit &&other)
    {
        if (this != &other) {
            destroy_engine();

            entry_point_ = other.entry_point_;
            stack_pointer_ = other.stack_pointer_;
            stack_ = other.stack_;
            llvm_execution_engine_ = other.llvm_execution_engine_;

            other.reset();
        }

        return *this;
    }

    uint16_t standalone_evm_jit::stack_pointer() const
    {
        assert(stack_pointer_ && "Stack pointer is null");
        return *stack_pointer_;
    }

    ::intx::uint256 standalone_evm_jit::stack(std::size_t idx) const
    {
        assert(idx <= stack_pointer() && "Out of bounds stack access");
        assert(stack_ && "Stack is null");
        return stack_[idx];
    }

    void standalone_evm_jit::operator()() const
    {
        assert(entry_point_ && "Entry point is null");
        entry_point_(nullptr, nullptr);
    }

    void standalone_evm_jit::destroy_engine()
    {
        if (llvm_execution_engine_) {
            static_cast<llvm::ExecutionEngine *>(llvm_execution_engine_)
                ->~ExecutionEngine();
        }
    }

    void standalone_evm_jit::reset()
    {
        entry_point_ = nullptr;
        stack_pointer_ = nullptr;
        stack_ = nullptr;
        llvm_execution_engine_ = nullptr;
    }
}
