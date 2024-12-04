#include <runtime/exit.h>
#include <runtime/types.h>

#include <exception>

// This implementation is currently a placeholder until more code-generation
// details are fleshed out; instead of aborting it will use hard-coded assembly
// to jump back to the contract epilogue and set return statuses appropriately.
extern "C" void runtime_exit
    [[noreturn]] (void *stack_ptr, monad::runtime::StatusCode error)
{
    (void)stack_ptr;
    (void)error;
    std::terminate();
}

namespace monad::runtime
{
    void Context::exit [[noreturn]] (StatusCode code) const noexcept
    {
        runtime_exit(exit_stack_ptr, code);
    }
}
