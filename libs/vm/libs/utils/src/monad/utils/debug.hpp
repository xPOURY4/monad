#pragma once

namespace monad::utils
{
#ifdef SAVE_EVM_STACK_ON_EXIT
    extern bool const debug_save_stack;
#else
    static constexpr bool debug_save_stack = false;
#endif
}
