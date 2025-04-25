#pragma once

#include <monad/vm/interpreter/intercode.hpp>
#include <monad/vm/interpreter/stack.hpp>
#include <monad/vm/interpreter/types.hpp>
#include <monad/vm/runtime/types.hpp>
#include <monad/vm/utils/uint256.hpp>

#include <evmc/evmc.h>

#include <cstdint>
#include <cstring>
#include <numeric>

namespace monad::vm::interpreter
{
    namespace detail
    {
        using subword_t = utils::uint256_t::word_type;

        // We need to do this memcpy dance to avoid triggering UB when
        // reading whole words from potentially unaligned addresses in the
        // instruction stream. The compilers seem able to optimise this out
        // effectively, and the generated code doesn't appear different to
        // the UB-triggering version.
        [[gnu::always_inline]] inline subword_t
        read_unaligned(std::uint8_t const *ptr)
        {
            alignas(subword_t) std::uint8_t aligned_mem[sizeof(subword_t)];
            std::memcpy(&aligned_mem[0], ptr, sizeof(subword_t));
            return std::byteswap(
                *reinterpret_cast<subword_t *>(&aligned_mem[0]));
        }
    }

    template <std::size_t N, evmc_revision Rev>
    struct push_impl
    {
        [[gnu::always_inline]] static inline OpcodeResult push(
            runtime::Context &ctx, Intercode const &analysis,
            utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
            std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
        {
            using namespace detail;

            static constexpr auto whole_words = N / 8;
            static constexpr auto leading_part = N % 8;

            check_requirements<PUSH0 + N, Rev>(
                ctx, analysis, stack_bottom, stack_top, gas_remaining);

            auto const leading_word = [instr_ptr] {
                auto word = subword_t{0};

                if constexpr (leading_part == 0) {
                    return word;
                }

                std::memcpy(
                    reinterpret_cast<std::uint8_t *>(&word) +
                        (8 - leading_part),
                    instr_ptr + 1,
                    leading_part);

                return std::byteswap(word);
            }();

            if constexpr (whole_words == 0) {
                interpreter::push(
                    stack_top, utils::uint256_t{leading_word, 0, 0, 0});
            }
            else if constexpr (whole_words == 1) {
                interpreter::push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(instr_ptr + 1 + leading_part),
                        leading_word,
                        0,
                        0,
                    });
            }
            else if constexpr (whole_words == 2) {
                interpreter::push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(instr_ptr + 1 + 8 + leading_part),
                        read_unaligned(instr_ptr + 1 + leading_part),
                        leading_word,
                        0,
                    });
            }
            else if constexpr (whole_words == 3) {
                interpreter::push(
                    stack_top,
                    utils::uint256_t{
                        read_unaligned(instr_ptr + 1 + 16 + leading_part),
                        read_unaligned(instr_ptr + 1 + 8 + leading_part),
                        read_unaligned(instr_ptr + 1 + leading_part),
                        leading_word,
                    });
            }

            return {gas_remaining, instr_ptr + N + 1};
        }
    };

    template <evmc_revision Rev>
    struct push_impl<0, Rev>
    {
        [[gnu::always_inline]] static inline OpcodeResult push(
            runtime::Context &ctx, Intercode const &analysis,
            utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
            std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
        {
            check_requirements<PUSH0, Rev>(
                ctx, analysis, stack_bottom, stack_top, gas_remaining);
            interpreter::push(stack_top, 0);
            return {gas_remaining, instr_ptr + 1};
        }
    };

    template <evmc_revision Rev>
    struct push_impl<32, Rev>
    {
        [[gnu::always_inline]] static inline OpcodeResult push(
            runtime::Context &ctx, Intercode const &analysis,
            utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
            std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
        {
            using namespace detail;

            check_requirements<PUSH32, Rev>(
                ctx, analysis, stack_bottom, stack_top, gas_remaining);

            interpreter::push(
                stack_top,
                utils::uint256_t{
                    read_unaligned(instr_ptr + 1 + 24),
                    read_unaligned(instr_ptr + 1 + 16),
                    read_unaligned(instr_ptr + 1 + 8),
                    read_unaligned(instr_ptr + 1),
                });

            return {gas_remaining, instr_ptr + 33};
        }
    };

    template <std::size_t N, evmc_revision Rev>
        requires(N <= 32)
    [[gnu::always_inline]] inline OpcodeResult push(
        runtime::Context &ctx, Intercode const &analysis,
        utils::uint256_t const *stack_bottom, utils::uint256_t *stack_top,
        std::int64_t gas_remaining, std::uint8_t const *instr_ptr)
    {
        return push_impl<N, Rev>::push(
            ctx, analysis, stack_bottom, stack_top, gas_remaining, instr_ptr);
    }
}
