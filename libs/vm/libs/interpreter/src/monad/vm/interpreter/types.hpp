#pragma once

#include <monad/vm/interpreter/state.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <array>
#include <cstdint>

namespace monad::vm::interpreter
{
    /**
     * Contains the state updated by each instruction, so that they can be put
     * explicitly in registers. Note that this structure is intended to be
     * returned by value in %rax and %rdx, and so must always be exactly 16
     * bytes in size.
     */
    struct OpcodeResult
    {
        std::int64_t gas_remaining;
        vm::utils::uint256_t *stack_top;
    };

    static_assert(sizeof(OpcodeResult) == 16);

    using InstrEval = OpcodeResult (*)(
        vm::runtime::Context &, State &, vm::utils::uint256_t const *,
        vm::utils::uint256_t *, std::int64_t);

    using InstrTable = std::array<InstrEval, 256>;
}
