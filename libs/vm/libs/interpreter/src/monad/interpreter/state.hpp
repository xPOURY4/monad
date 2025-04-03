#pragma once

#include <monad/interpreter/intercode.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <cstdint>
#include <tuple>

namespace monad::interpreter
{
    struct State
    {
        State(Intercode const &code)
            : analysis{code}
            , instr_ptr{analysis.code()}
        {
        }

        [[gnu::always_inline]] inline void next()
        {
            instr_ptr++;
        }

        Intercode const &analysis;
        std::uint8_t const *instr_ptr;
    };
}
