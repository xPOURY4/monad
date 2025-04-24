#pragma once

#include <monad/vm/runtime/runtime.hpp>

#include <asmjit/x86.h>

#include <functional>

namespace monad::vm::compiler::native
{
    using entrypoint_t = void (*)(runtime::Context *, uint8_t *);

    class Nativecode
    {
    public:
        enum ErrorCode
        {
            NoError,
            Unexpected,
            SizeOutOfBounds
        };

        /// If compilation failed, then `entrypoint` is `nullptr`.
        Nativecode(
            asmjit::JitRuntime &asmjit_rt, entrypoint_t entry,
            size_t native_code_size_estimate)
            : asmjit_rt_{asmjit_rt}
            , entrypoint_{entry}
            , native_code_size_estimate_{native_code_size_estimate}
        {
        }

        Nativecode(Nativecode const &) = delete;
        Nativecode &operator=(Nativecode const &) = delete;

        /// Get native entry point, or `nullptr` if compilation failed.
        entrypoint_t entrypoint() const
        {
            return entrypoint_;
        }

        size_t native_code_size_estimate() const
        {
            return entrypoint_ ? native_code_size_estimate_ : 0;
        }

        ErrorCode error_code() const
        {
            if (entrypoint_) {
                return NoError;
            }
            if (native_code_size_estimate_ > 0) {
                return SizeOutOfBounds;
            }
            return Unexpected;
        }

        ~Nativecode()
        {
            if (entrypoint_) {
                asmjit_rt_.release(entrypoint_);
            }
        }

    private:
        asmjit::JitRuntime &asmjit_rt_;
        entrypoint_t entrypoint_;
        size_t native_code_size_estimate_;
    };

    class Emitter;

    using EmitterHook = std::function<void(Emitter &)>;

    struct CompilerConfig
    {
        char const *asm_log_path{};
        bool runtime_debug_trace{};
        bool verbose{};
        EmitterHook post_instruction_emit_hook{};
    };
}
