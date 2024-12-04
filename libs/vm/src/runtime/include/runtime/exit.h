#include <runtime/types.h>

extern "C" void runtime_exit
    [[noreturn]] (void *stack_ptr, monad::runtime::StatusCode status);
