#pragma once

#include <monad/vm/interpreter/intercode.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/runtime/uint256.hpp>

#include <array>
#include <cstdint>

namespace monad::vm::interpreter
{
    using InstrEval = void (*)(
        runtime::Context &, Intercode const &, utils::uint256_t const *,
        utils::uint256_t *, std::int64_t, std::uint8_t const *);

    using InstrTable = std::array<InstrEval, 256>;
}
