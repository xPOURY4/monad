#include <runtime/exit.h>
#include <runtime/types.h>

#include <exception>

// This implementation is currently a placeholder until more code-generation
// details are fleshed out; instead of aborting it will use hard-coded assembly
// to jump back to the contract epilogue and set return statuses appropriately.
extern "C" void runtime_exit [[noreturn]] (
    void *stack_ptr, monad::runtime::Context *ctx, monad::runtime::Error error)
{
    (void)stack_ptr;
    (void)ctx;
    (void)error;
    std::terminate();
}
