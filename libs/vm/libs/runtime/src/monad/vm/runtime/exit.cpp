#include <monad/vm/runtime/types.hpp>

extern "C" void monad_vm_runtime_exit [[noreturn]] (void *);

namespace monad::vm::runtime
{
    void Context::exit [[noreturn]] (StatusCode code) noexcept
    {
        result.status = code;
        monad_vm_runtime_exit(exit_stack_ptr);
    }
}
