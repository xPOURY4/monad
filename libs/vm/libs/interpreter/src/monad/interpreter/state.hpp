#pragma once

#include <monad/interpreter/intercode.hpp>
#include <monad/runtime/types.hpp>
#include <monad/utils/uint256.hpp>

#include <cstdint>

namespace monad::interpreter
{
    struct State
    {
        State(
            Intercode const &code, runtime::Context const &ctx,
            std::uint8_t *stack_ptr)
            : analysis{code}
            , instr_ptr{ctx.env.code}
            , stack_top{reinterpret_cast<utils::uint256_t *>(stack_ptr) - 1}
            , stack_size{0}
        {
        }

        utils::uint256_t const &pop()
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size > 0);
            stack_size -= 1;
            stack_top -= 1;
            return *(stack_top + 1);
        }

        utils::uint256_t &top()
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size > 0);
            return *stack_top;
        }

        void push(utils::uint256_t const &x)
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size < 1024);
            stack_size += 1;
            stack_top += 1;
            top() = x;
        }

        Intercode const &analysis;
        std::uint8_t const *instr_ptr;
        utils::uint256_t *stack_top;
        std::uint16_t stack_size;
    };
}
