#pragma once

namespace monad::vm::utils
{
#ifdef MONAD_COMPILER_TESTING
    extern bool const is_fuzzing_monad_compiler;
#else
    static constexpr bool is_fuzzing_monad_compiler = false;
#endif
}
