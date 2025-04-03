#pragma once

#include <monad/interpreter/state.hpp>
#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/runtime/types.hpp>

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
        std::uint8_t const instr, vm::runtime::Context const &ctx,
        State const &state, vm::utils::uint256_t const *stack_bottom,
        vm::utils::uint256_t const *stack_top, std::uint64_t gas_remaining)
    {
        auto const &info = compiler::opcode_table<Rev>[instr];
        auto const stack_size = stack_top - stack_bottom;

        std::cerr << std::format(
            "{{\"pc\":{},\"op\":{},\"gas\":\"0x{:x}\",\"gasCost\":\"0x{"
            ":x}\",\"memSize\":{},\"stack\":[",
            state.instr_ptr - state.analysis.code(),
            instr,
            gas_remaining,
            info.dynamic_gas ? 0 : info.min_gas,
            ctx.memory.size);

        auto comma = "";
        for (auto i = stack_size - 1; i >= 0; --i) {
            std::cerr << std::format(
                "{}\"0x{}\"", comma, intx::to_string(*(stack_top - i), 16));
            comma = ",";
        }

        std::cerr << std::format(
            "],\"depth\":{},\"refund\":{},\"opName\":\"{}\"}}\n",
            ctx.env.depth,
            ctx.gas_refund,
            info.name);
    }
}
