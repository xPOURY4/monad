#include <monad/vm/core/assert.h>
#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/evm/opcodes_xmacro.hpp>
#include <monad/vm/interpreter/debug.hpp>
#include <monad/vm/interpreter/execute.hpp>
#include <monad/vm/interpreter/instruction_stats.hpp>
#include <monad/vm/interpreter/instruction_table.hpp>
#include <monad/vm/interpreter/intercode.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/traits.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <array>
#include <cstdint>
#include <cstdlib>

/**
 * Assembly trampoline into the interpreter's core loop (see entry.S). This
 * function sets up the stack to be compatible with the runtime's exit ABI, then
 * jumps to `monad_vm_interpreter_core_loop`. It is therefore important that
 * these two functions always maintain the same signature (so that arguments are
 * in the expected registers when jumping to the core loop).
 */
extern "C" void monad_vm_interpreter_trampoline(
    void *, evmc_revision, ::monad::vm::runtime::Context *,
    ::monad::vm::interpreter::Intercode const *,
    ::monad::vm::utils::uint256_t *);

extern "C" void monad_vm_interpreter_core_loop(
    void *, evmc_revision, ::monad::vm::runtime::Context *,
    ::monad::vm::interpreter::Intercode const *,
    ::monad::vm::utils::uint256_t *);

static_assert(
    ::monad::vm::utils::same_signature(
        monad_vm_interpreter_trampoline, monad_vm_interpreter_core_loop),
    "Interpreter core loop and trampoline signatures must be identical");

#define ASM_COMMENT(C) asm volatile("# " C);

namespace monad::vm::interpreter
{
    namespace
    {
        template <evmc_revision Rev>
        void core_loop_impl(
            runtime::Context &ctx, Intercode const &analysis,
            utils::uint256_t *stack_ptr)
        {
            static constexpr auto dispatch_table = std::array{
#define MONAD_COMPILER_ON_EVM_OPCODE(op) &&LABEL_##op,
                MONAD_COMPILER_EVM_ALL_OPCODES
#undef MONAD_COMPILER_ON_EVM_OPCODE
            };
            static_assert(dispatch_table.size() == 256);

            auto *stack_top = stack_ptr - 1;
            auto const *const stack_bottom = stack_top;
            auto const *instr_ptr = analysis.code();

            auto gas_remaining = ctx.gas_remaining;

            goto *dispatch_table[*instr_ptr];

#define MONAD_COMPILER_ON_EVM_OPCODE(op)                                       \
    LABEL_##op:                                                                \
    {                                                                          \
        ASM_COMMENT("OPCODE: " #op);                                           \
                                                                               \
        stats::begin(op);                                                      \
                                                                               \
        if constexpr (debug_enabled) {                                         \
            trace<Rev>(                                                        \
                op,                                                            \
                ctx,                                                           \
                analysis,                                                      \
                stack_bottom,                                                  \
                stack_top,                                                     \
                gas_remaining,                                                 \
                instr_ptr);                                                    \
        }                                                                      \
                                                                               \
        static constexpr auto eval = instruction_table<Rev>[op];               \
        auto const [gas_rem, ip] = eval(                                       \
            ctx, analysis, stack_bottom, stack_top, gas_remaining, instr_ptr); \
                                                                               \
        static constexpr auto delta =                                          \
            compiler::opcode_table<Rev>[op].stack_increase -                   \
            compiler::opcode_table<Rev>[op].min_stack;                         \
                                                                               \
        gas_remaining = gas_rem;                                               \
        instr_ptr = ip;                                                        \
        stack_top = stack_top + delta;                                         \
                                                                               \
        stats::end();                                                          \
                                                                               \
        goto *dispatch_table[*instr_ptr];                                      \
    }
            MONAD_COMPILER_EVM_ALL_OPCODES
#undef MONAD_COMPILER_ON_EVM_OPCODE

            MONAD_VM_ASSERT(false);
        }
    }

    void execute(
        evmc_revision rev, runtime::Context &ctx, Intercode const &analysis,
        std::uint8_t *stack_ptr)
    {
        monad_vm_interpreter_trampoline(
            &ctx.exit_stack_ptr,
            rev,
            &ctx,
            &analysis,
            reinterpret_cast<utils::uint256_t *>(stack_ptr));
    }
}

extern "C" void monad_vm_interpreter_core_loop(
    void *, evmc_revision rev, ::monad::vm::runtime::Context *ctx,
    ::monad::vm::interpreter::Intercode const *analysis,
    ::monad::vm::utils::uint256_t *stack_ptr)
{
    using namespace ::monad::vm::interpreter;

    switch (rev) {
    case EVMC_FRONTIER:
        return core_loop_impl<EVMC_FRONTIER>(*ctx, *analysis, stack_ptr);
    case EVMC_HOMESTEAD:
        return core_loop_impl<EVMC_HOMESTEAD>(*ctx, *analysis, stack_ptr);
    case EVMC_TANGERINE_WHISTLE:
        return core_loop_impl<EVMC_TANGERINE_WHISTLE>(
            *ctx, *analysis, stack_ptr);
    case EVMC_SPURIOUS_DRAGON:
        return core_loop_impl<EVMC_SPURIOUS_DRAGON>(*ctx, *analysis, stack_ptr);
    case EVMC_BYZANTIUM:
        return core_loop_impl<EVMC_BYZANTIUM>(*ctx, *analysis, stack_ptr);
    case EVMC_CONSTANTINOPLE:
        return core_loop_impl<EVMC_CONSTANTINOPLE>(*ctx, *analysis, stack_ptr);
    case EVMC_PETERSBURG:
        return core_loop_impl<EVMC_PETERSBURG>(*ctx, *analysis, stack_ptr);
    case EVMC_ISTANBUL:
        return core_loop_impl<EVMC_ISTANBUL>(*ctx, *analysis, stack_ptr);
    case EVMC_BERLIN:
        return core_loop_impl<EVMC_BERLIN>(*ctx, *analysis, stack_ptr);
    case EVMC_LONDON:
        return core_loop_impl<EVMC_LONDON>(*ctx, *analysis, stack_ptr);
    case EVMC_PARIS:
        return core_loop_impl<EVMC_PARIS>(*ctx, *analysis, stack_ptr);
    case EVMC_SHANGHAI:
        return core_loop_impl<EVMC_SHANGHAI>(*ctx, *analysis, stack_ptr);
    case EVMC_CANCUN:
        return core_loop_impl<EVMC_CANCUN>(*ctx, *analysis, stack_ptr);
    default:
        MONAD_VM_ASSERT(false);
    }
}
