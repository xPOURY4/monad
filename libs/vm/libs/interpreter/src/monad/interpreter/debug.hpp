#pragma once

#include <monad/evm/opcodes.hpp>
#include <monad/interpreter/state.hpp>
#include <monad/runtime/types.hpp>

#include <evmc/evmc.h>

#include <format>
#include <iostream>

namespace monad::interpreter
{
#ifdef MONAD_INTERPRETER_DEBUG
    constexpr auto debug_enabled = true;
#else
    constexpr auto debug_enabled = false;
#endif

    /**
     * Debug trace printing compatible with the JSON format emitted by evmone.
     */
    template <evmc_revision Rev>
    void trace(
        std::uint8_t const instr, runtime::Context const &ctx,
        State const &state)
    {
        auto const &info = compiler::opcode_table<Rev>[instr];

        std::cerr << std::format(
            "{{\"pc\":{},\"op\":{},\"gas\":\"0x{:x}\",\"gasCost\":\"0x{"
            ":x}\",\"memSize\":{},\"stack\":[",
            state.instr_ptr - state.analysis.code(),
            instr,
            ctx.gas_remaining,
            info.dynamic_gas ? 0 : info.min_gas,
            ctx.memory.size);

        auto comma = "";
        for (auto i = state.stack_size() - 1; i >= 0; --i) {
            std::cerr << std::format(
                "{}\"0x{}\"",
                comma,
                intx::to_string(*(state.stack_top - i), 16));
            comma = ",";
        }

        std::cerr << std::format(
            "],\"depth\":{},\"refund\":{},\"opName\":\"{}\"}}\n",
            ctx.env.depth,
            ctx.gas_refund,
            info.name);
    }
}
