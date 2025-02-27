#include <monad/runtime/types.hpp>

extern "C" void monad_runtime_exit [[noreturn]] (void *);

namespace monad::runtime
{
    void Context::exit [[noreturn]] (StatusCode code) noexcept
    {
        result.status = code;
        monad_runtime_exit(exit_stack_ptr);
    }
}
