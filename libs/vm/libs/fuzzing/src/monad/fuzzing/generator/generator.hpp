#pragma once

#include <monad/fuzzing/generator/choice.hpp>
#include <monad/fuzzing/generator/instruction_data.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/cases.hpp>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.hpp>

#include <array>
#include <limits>
#include <random>
#include <unordered_set>
#include <variant>
#include <vector>

namespace monad::fuzzing
{
    struct ValidAddress
    {
    };

    struct ValidJumpDest
    {
    };

    struct Constant
    {
        utils::uint256_t value;
    };

    template <typename Engine>
    Constant meaningful_constant(Engine &gen)
    {
        constexpr auto values = std::array<utils::uint256_t, 4>{
            0,
            1,
            std::numeric_limits<utils::uint256_t>::max() - 1,
            std::numeric_limits<utils::uint256_t>::max(),
        };

        return Constant{uniform_sample(gen, values)};
    }

    template <typename Engine>
    Constant power_of_two_constant(Engine &gen)
    {
        auto dist = std::uniform_int_distribution(1, 254);
        return Constant{
            intx::exp(utils::uint256_t(2), utils::uint256_t(dist(gen)))};
    }

    template <typename Engine>
    Constant random_constant(Engine &gen)
    {
        auto dist =
            std::uniform_int_distribution<utils::uint256_t::word_type>();

        return Constant{
            utils::uint256_t{dist(gen), dist(gen), dist(gen), dist(gen)}};
    }

    using Push = std::variant<ValidAddress, ValidJumpDest, Constant>;

    template <typename Engine>
    Push generate_push(Engine &eng)
    {
        return uniform_choice<Push>(
            eng,
            [](auto &g) { return random_constant(g); },
            Choice(0.25, [](auto &) { return ValidJumpDest{}; }),
            Choice(0.25, [](auto &) { return ValidAddress{}; }),
            Choice(0.20, [](auto &g) { return meaningful_constant(g); }),
            Choice(0.20, [](auto &g) { return power_of_two_constant(g); }));
    }

    struct NonTerminator
    {
        std::uint8_t opcode;
    };

    struct Terminator
    {
        std::uint8_t opcode;
    };

    using Instruction = std::variant<NonTerminator, Terminator, Push>;

    template <typename Engine>
    NonTerminator generate_safe_non_terminator(Engine &eng)
    {
        return NonTerminator{uniform_sample(eng, safe_non_terminators)};
    }

    template <typename Engine>
    Terminator generate_terminator(Engine &eng, bool const exit)
    {
        auto opcode = exit ? uniform_sample(eng, exit_terminators)
                           : uniform_sample(eng, jump_terminators);

        return Terminator{opcode};
    }

    template <typename Engine>
    NonTerminator generate_random_byte(Engine &eng)
    {
        auto dist = std::uniform_int_distribution<std::uint8_t>();
        return NonTerminator{dist(eng)};
    }

    template <typename Engine>
    std::vector<Instruction>
    generate_block(Engine &eng, bool const is_exit, bool const is_main)
    {
        constexpr std::size_t max_block_insts = 1000;

        auto program = std::vector<Instruction>{};

        constexpr auto total_non_term_prob = 0.98;
        constexpr auto push_weight = (32.0 / 148.0);
        constexpr auto non_term_weight = 1.0 - push_weight;

        constexpr auto push_prob = total_non_term_prob * push_weight;
        constexpr auto non_term_prob = total_non_term_prob * non_term_weight;

        if (is_main) {
            constexpr auto main_initial_pushes = 24;

            for (auto i = 0; i < main_initial_pushes; ++i) {
                program.push_back(generate_push(eng));
            }
        }

        with_probability(eng, 0.8, [&](auto &) {
            program.push_back(NonTerminator{JUMPDEST});
        });

        for (auto terminated = false;
             !terminated && program.size() <= max_block_insts;) {
            auto next_inst = uniform_choice<Instruction>(
                eng,
                [](auto &g) { return generate_random_byte(g); },
                Choice(
                    non_term_prob,
                    [](auto &g) { return generate_safe_non_terminator(g); }),
                Choice(push_prob, [](auto &g) { return generate_push(g); }),
                Choice(0.019999, [&](auto &g) {
                    return generate_terminator(g, is_exit);
                }));

            if (auto *term = std::get_if<Terminator>(&next_inst)) {
                terminated = true;

                auto op = term->opcode;

                if (op == JUMP || op == JUMPI) {
                    with_probability(eng, 0.5, [&](auto &) {
                        program.push_back(ValidJumpDest{});
                    });
                }
                else if (is_exit_terminator(op)) {
                    for (auto op : {SSTORE, MSTORE}) {
                        with_probability(eng, 0.293, [&](auto &) {
                            program.push_back(NonTerminator{op});
                        });
                    }
                }
            }

            program.emplace_back(std::move(next_inst));
        }

        return program;
    }

    std::vector<std::uint8_t> generate_program();
}
