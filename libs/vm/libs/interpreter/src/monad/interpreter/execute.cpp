#include <monad/evm/opcodes_xmacro.hpp>
#include <monad/interpreter/debug.hpp>
#include <monad/interpreter/execute.hpp>
#include <monad/interpreter/instruction_table.hpp>
#include <monad/interpreter/intercode.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/types.hpp>
#include <monad/vm/core/assert.h>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <span>

/**
 * Assembly trampoline into the interpreter's core loop (see entry.S). This
 * function sets up the stack to be compatible with the runtime's exit ABI, then
 * jumps to `interpreter_core_loop`. It is therefore important that these two
 * functions always maintain the same signature (so that arguments are in the
 * expected registers when jumping to the core loop).
 */
extern "C" void interpreter_runtime_trampoline(
    void *, evmc_revision, monad::runtime::Context *,
    monad::interpreter::State *, monad::vm::utils::uint256_t *);

extern "C" void interpreter_core_loop(
    void *, evmc_revision, monad::runtime::Context *,
    monad::interpreter::State *, monad::vm::utils::uint256_t *);

namespace monad::interpreter
{
    namespace
    {
        template <evmc_revision Rev>
        void core_loop_impl(
            runtime::Context &ctx, State &state,
            vm::utils::uint256_t *stack_ptr)
        {
            static constexpr auto dispatch_table = std::array{
#define MONAD_COMPILER_ON_EVM_OPCODE(op) &&LABEL_##op,
                MONAD_COMPILER_EVM_ALL_OPCODES
#undef MONAD_COMPILER_ON_EVM_OPCODE
            };
            static_assert(dispatch_table.size() == 256);

            auto *stack_top = stack_ptr - 1;
            auto const *stack_bottom = stack_top;

            auto gas_remaining = ctx.gas_remaining;

            goto *dispatch_table[*state.instr_ptr];

#define MONAD_COMPILER_ON_EVM_OPCODE(op)                                       \
    LABEL_##op:                                                                \
    {                                                                          \
        if constexpr (debug_enabled) {                                         \
            trace<Rev>(                                                        \
                op, ctx, state, stack_bottom, stack_top, gas_remaining);       \
        }                                                                      \
                                                                               \
        static constexpr auto eval = instruction_table<Rev>[op];               \
        auto const [gas_rem, top] =                                            \
            eval(ctx, state, stack_bottom, stack_top, gas_remaining);          \
                                                                               \
        gas_remaining = gas_rem;                                               \
        stack_top = top;                                                       \
                                                                               \
        goto *dispatch_table[*state.instr_ptr];                                \
    }
            MONAD_COMPILER_EVM_ALL_OPCODES
#undef MONAD_COMPILER_ON_EVM_OPCODE

            MONAD_VM_ASSERT(false);
        }

        std::unique_ptr<std::uint8_t, decltype(std::free) *> allocate_stack()
        {
            return {
                reinterpret_cast<std::uint8_t *>(std::aligned_alloc(
                    32, sizeof(vm::utils::uint256_t) * 1024)),
                std::free};
        }
    }

    evmc_result execute(
        evmc_host_interface const *host, evmc_host_context *context,
        evmc_revision rev, evmc_message const *msg,
        std::span<uint8_t const> code)
    {
        auto ctx = runtime::Context::from(host, context, msg, code);

        auto const stack_ptr = allocate_stack();
        auto const analysis = Intercode(code);
        auto state = State(analysis);

        interpreter_runtime_trampoline(
            &ctx.exit_stack_ptr,
            rev,
            &ctx,
            &state,
            reinterpret_cast<vm::utils::uint256_t *>(stack_ptr.get()));

        return ctx.copy_to_evmc_result();
    }
}

extern "C" void interpreter_core_loop(
    void *, evmc_revision rev, monad::runtime::Context *ctx,
    monad::interpreter::State *state, monad::vm::utils::uint256_t *stack_ptr)
{
    using namespace monad::interpreter;

    switch (rev) {
    case EVMC_FRONTIER:
        return core_loop_impl<EVMC_FRONTIER>(*ctx, *state, stack_ptr);
    case EVMC_HOMESTEAD:
        return core_loop_impl<EVMC_HOMESTEAD>(*ctx, *state, stack_ptr);
    case EVMC_TANGERINE_WHISTLE:
        return core_loop_impl<EVMC_TANGERINE_WHISTLE>(*ctx, *state, stack_ptr);
    case EVMC_SPURIOUS_DRAGON:
        return core_loop_impl<EVMC_SPURIOUS_DRAGON>(*ctx, *state, stack_ptr);
    case EVMC_BYZANTIUM:
        return core_loop_impl<EVMC_BYZANTIUM>(*ctx, *state, stack_ptr);
    case EVMC_CONSTANTINOPLE:
        return core_loop_impl<EVMC_CONSTANTINOPLE>(*ctx, *state, stack_ptr);
    case EVMC_PETERSBURG:
        return core_loop_impl<EVMC_PETERSBURG>(*ctx, *state, stack_ptr);
    case EVMC_ISTANBUL:
        return core_loop_impl<EVMC_ISTANBUL>(*ctx, *state, stack_ptr);
    case EVMC_BERLIN:
        return core_loop_impl<EVMC_BERLIN>(*ctx, *state, stack_ptr);
    case EVMC_LONDON:
        return core_loop_impl<EVMC_LONDON>(*ctx, *state, stack_ptr);
    case EVMC_PARIS:
        return core_loop_impl<EVMC_PARIS>(*ctx, *state, stack_ptr);
    case EVMC_SHANGHAI:
        return core_loop_impl<EVMC_SHANGHAI>(*ctx, *state, stack_ptr);
    case EVMC_CANCUN:
        return core_loop_impl<EVMC_CANCUN>(*ctx, *state, stack_ptr);
    default:
        MONAD_VM_ASSERT(false);
    }
}
