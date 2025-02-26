#include <monad/runtime/types.hpp>

#include <csetjmp>

extern "C" void monad_runtime_exit [[noreturn]] (void *);

namespace monad::runtime
{
    void Context::exit [[noreturn]] (StatusCode code) noexcept
    {
        result.status = code;

        if (exit_stack_ptr) {
            monad_runtime_exit(exit_stack_ptr);
        }
        else {
            std::longjmp(exit_buffer, 1);
        }
    }
}
