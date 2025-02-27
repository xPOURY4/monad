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
            , stack_bottom{stack_top}
        {
        }

        [[gnu::always_inline]]
        inline utils::uint256_t const &pop()
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size() > 0);
            auto const &ret = *stack_top;
            stack_top -= 1;
            return ret;
        }

        [[gnu::always_inline]]
        inline utils::uint256_t &top()
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size() > 0);
            return *stack_top;
        }

        [[gnu::always_inline]]
        inline void push(utils::uint256_t const &x)
        {
            MONAD_COMPILER_DEBUG_ASSERT(stack_size() < 1024);
            stack_top += 1;
            top() = x;
        }

        std::ptrdiff_t stack_size() const noexcept
        {
            return stack_top - stack_bottom;
        }

        Intercode const &analysis;
        std::uint8_t const *instr_ptr;
        utils::uint256_t *stack_top;
        utils::uint256_t *stack_bottom;
    };
}
