#include <cstdlib>
#include <cstring>

namespace monad::utils
{
#ifdef SAVE_EVM_STACK_ON_EXIT
    static auto const debug_save_stack_env =
        std::getenv("SAVE_EVM_STACK_ON_EXIT");
    bool const debug_save_stack =
        debug_save_stack_env && std::strcmp(debug_save_stack_env, "1") == 0;
#endif
}
