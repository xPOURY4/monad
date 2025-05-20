#pragma once

#include <monad/vm/interpreter/intercode.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/runtime/uint256.hpp>

#include <array>
#include <cstdint>

/**
 * This attribute changes the calling convention of the tail-called instruction
 * dispatch functions so that their pinned arguments are passed in different
 * registers to the usual SysV ABI. Doing so means that we perform far less
 * shuffling of arguments when making calls into the runtime: the non-tail
 * runtime function calls get to use the regular SysV registers, and must
 * preserve the registers used for argument threading.
 *
 * See https://blog.reverberate.org/2025/02/10/tail-call-updates.html for a good
 * reference to this technique. It is not supported by GCC as of version 15.
 */
#if defined(__has_attribute)
    #if __has_attribute(preserve_none)
        #define MONAD_VM_INSTRUCTION_CALL __attribute__((preserve_none))
    #else
        #define MONAD_VM_INSTRUCTION_CALL
    #endif
#else
    #define MONAD_VM_INSTRUCTION_CALL
#endif

namespace monad::vm::interpreter
{
    using InstrEval = void MONAD_VM_INSTRUCTION_CALL (*)(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    using InstrTable = std::array<InstrEval, 256>;
}
