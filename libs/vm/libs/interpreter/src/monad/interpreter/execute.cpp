#include <monad/evm/opcodes.hpp>
#include <monad/interpreter/debug.hpp>
#include <monad/interpreter/execute.hpp>
#include <monad/interpreter/instruction_table.hpp>
#include <monad/interpreter/intercode.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.h>

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
    monad::interpreter::State *);

extern "C" void interpreter_core_loop(
    void *, evmc_revision, monad::runtime::Context *,
    monad::interpreter::State *);

namespace monad::interpreter
{
    namespace
    {
        template <evmc_revision Rev>
        void charge_static_gas(std::uint8_t const instr, runtime::Context &ctx)
        {
            ctx.gas_remaining -= compiler::opcode_table<Rev>[instr].min_gas;

            if (MONAD_COMPILER_UNLIKELY(ctx.gas_remaining < 0)) {
                ctx.exit(runtime::StatusCode::Error);
            }
        }

        template <evmc_revision Rev>
        void check_stack(
            std::uint8_t const instr, runtime::Context &ctx, State &state,
            utils::uint256_t const *stack_bottom)
        {
            auto const &info = compiler::opcode_table<Rev>[instr];
            auto const stack_size = state.stack_top - stack_bottom;

            if (MONAD_COMPILER_UNLIKELY(
                    stack_size < info.min_stack ||
                    (stack_size - info.min_stack + info.increases_stack >
                     1024))) {
                ctx.exit(runtime::StatusCode::Error);
            }
        }

        template <evmc_revision Rev>
        void core_loop_impl(runtime::Context &ctx, State &state)
        {
            while (true) {
                auto const instr = *state.instr_ptr;

                if constexpr (debug_enabled) {
                    trace<Rev>(instr, ctx, state);
                }

                instruction_table<Rev>[instr](ctx, state);
            }
        }

        std::unique_ptr<std::uint8_t, decltype(std::free) *> allocate_stack()
        {
            return {
                reinterpret_cast<std::uint8_t *>(
                    std::aligned_alloc(32, sizeof(utils::uint256_t) * 1024)),
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
        auto state = State{analysis, stack_ptr.get()};

        interpreter_runtime_trampoline(&ctx.exit_stack_ptr, rev, &ctx, &state);
        return ctx.copy_to_evmc_result();
    }
}

extern "C" void interpreter_core_loop(
    void *, evmc_revision rev, monad::runtime::Context *ctx,
    monad::interpreter::State *state)
{
    using namespace monad::interpreter;

    switch (rev) {
    case EVMC_FRONTIER:
        return core_loop_impl<EVMC_FRONTIER>(*ctx, *state);
    case EVMC_HOMESTEAD:
        return core_loop_impl<EVMC_HOMESTEAD>(*ctx, *state);
    case EVMC_TANGERINE_WHISTLE:
        return core_loop_impl<EVMC_TANGERINE_WHISTLE>(*ctx, *state);
    case EVMC_SPURIOUS_DRAGON:
        return core_loop_impl<EVMC_SPURIOUS_DRAGON>(*ctx, *state);
    case EVMC_BYZANTIUM:
        return core_loop_impl<EVMC_BYZANTIUM>(*ctx, *state);
    case EVMC_CONSTANTINOPLE:
        return core_loop_impl<EVMC_CONSTANTINOPLE>(*ctx, *state);
    case EVMC_PETERSBURG:
        return core_loop_impl<EVMC_PETERSBURG>(*ctx, *state);
    case EVMC_ISTANBUL:
        return core_loop_impl<EVMC_ISTANBUL>(*ctx, *state);
    case EVMC_BERLIN:
        return core_loop_impl<EVMC_BERLIN>(*ctx, *state);
    case EVMC_LONDON:
        return core_loop_impl<EVMC_LONDON>(*ctx, *state);
    case EVMC_PARIS:
        return core_loop_impl<EVMC_PARIS>(*ctx, *state);
    case EVMC_SHANGHAI:
        return core_loop_impl<EVMC_SHANGHAI>(*ctx, *state);
    case EVMC_CANCUN:
        return core_loop_impl<EVMC_CANCUN>(*ctx, *state);
    default:
        MONAD_COMPILER_ASSERT(false);
    }
}
