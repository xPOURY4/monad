#pragma once

#include <monad/vm/runtime/runtime.hpp>

#include <functional>

namespace monad::vm::compiler::native
{
    using entrypoint_t = void (*)(vm::runtime::Context *, uint8_t *);

    class Emitter;

    using EmitterHook = std::function<void(Emitter &)>;

    struct CompilerConfig
    {
        char const *asm_log_path{};
        bool runtime_debug_trace{};
        EmitterHook post_instruction_emit_hook{};
    };
}
