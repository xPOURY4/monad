#pragma once

#include <monad/vm/interpreter/intercode.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/runtime/uint256.hpp>

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
        std::uint8_t const *instr_ptr;
    };

    static_assert(sizeof(OpcodeResult) == 16);

    using InstrEval = OpcodeResult (*)(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    using InstrTable = std::array<InstrEval, 256>;
}
