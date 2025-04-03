#include <cstdlib>
#include <cstring>

namespace monad::vm::utils
{
#ifdef MONAD_COMPILER_TESTING
    static auto const is_fuzzing_monad_compiler_env =
        std::getenv("MONAD_COMPILER_FUZZING");
    bool is_fuzzing_monad_compiler =
        is_fuzzing_monad_compiler_env &&
        std::strcmp(is_fuzzing_monad_compiler_env, "1") == 0;
#endif
}
