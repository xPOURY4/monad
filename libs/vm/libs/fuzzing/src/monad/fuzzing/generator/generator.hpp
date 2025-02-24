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

using namespace evmc::literals;

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

    template <typename Engine>
    Constant memory_constant(Engine &gen)
    {
        auto dist = std::uniform_int_distribution<std::uint64_t>(0, 1 << 16);
        return Constant{dist(gen)};
    }

    using Push = std::variant<ValidAddress, ValidJumpDest, Constant>;

    template <typename Engine>
    Push generate_push(Engine &eng)
    {
        return discrete_choice<Push>(
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

        // Parameters chosen based on the initial fuzzer specification. Because
        // we generate pushes using a different method to other non-terminator
        // instructions, we need to weight their generation probability
        // proportionately to the total number of EVM opcodes. This could be
        // changed in the future to reconfigure the number of pushes vs. other
        // instructions.
        constexpr auto total_non_term_prob = 0.90;
        constexpr auto push_weight = (32.0 / 148.0);
        constexpr auto non_term_weight = 1.0 - push_weight;

        constexpr auto push_prob = total_non_term_prob * push_weight;
        constexpr auto non_term_prob = total_non_term_prob * non_term_weight;

        constexpr auto random_byte_prob = 0.000001;
        constexpr auto terminate_prob =
            (1 - total_non_term_prob) - random_byte_prob;

        if (is_main) {
            // Parameters chosen by eye; roughly 10% chance of 12 or fewer
            // pushes and 95% chance of 24 or fewer. Could be configured to
            // change the characteristics of this distribution.
            auto main_pushes_dist =
                std::binomial_distribution<std::size_t>(50, 0.35);
            auto const main_initial_pushes = main_pushes_dist(eng);

            for (auto i = 0u; i < main_initial_pushes; ++i) {
                program.push_back(generate_push(eng));
            }
        }

        with_probability(eng, 0.8, [&](auto &) {
            program.push_back(NonTerminator{JUMPDEST});
        });

        for (auto terminated = false;
             !terminated && program.size() <= max_block_insts;) {
            auto next_inst = discrete_choice<Instruction>(
                eng,
                [](auto &g) { return generate_random_byte(g); },
                Choice(
                    non_term_prob,
                    [](auto &g) { return generate_safe_non_terminator(g); }),
                Choice(push_prob, [](auto &g) { return generate_push(g); }),
                Choice(terminate_prob, [&](auto &g) {
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

    template <typename Engine>
    void compile_push(
        Engine &eng, std::vector<std::uint8_t> &program, Push const &push,
        std::vector<evmc::address> const &valid_addresses,
        std::vector<std::size_t> &jumpdest_patches)
    {
        std::visit(
            utils::Cases{
                [&](ValidAddress) {
                    if (valid_addresses.empty()) {
                        return;
                    }

                    auto const &addr = uniform_sample(eng, valid_addresses);

                    program.push_back(PUSH20);
                    for (auto b : addr.bytes) {
                        program.push_back(b);
                    }
                },
                [&](ValidJumpDest) {
                    jumpdest_patches.push_back(program.size());

                    program.push_back(PUSH4);
                    for (auto i = 0; i < 4; ++i) {
                        program.push_back(0xFF);
                    }
                },
                [&](Constant const &c) {
                    program.push_back(PUSH32);

                    auto const *bs = intx::as_bytes(c.value);
                    for (auto i = 31; i >= 0; --i) {
                        program.push_back(bs[i]);
                    }
                },
            },
            push);
    }

    template <typename Engine>
    void compile_block(
        Engine &eng, std::vector<std::uint8_t> &program,
        std::vector<Instruction> const &block,
        std::vector<evmc::address> const &valid_addresses,
        std::vector<std::uint32_t> &valid_jumpdests,
        std::vector<std::size_t> &jumpdest_patches)
    {
        auto push_op = [&](auto op) {
            if (op == JUMPDEST) {
                valid_jumpdests.push_back(
                    static_cast<std::uint32_t>(program.size()));
            }

            for (auto mem_op : memory_operands(op)) {
                with_probability(eng, 0.95, [&](auto &) {
                    auto const safe_value = memory_constant(eng);

                    auto const byte_size =
                        intx::count_significant_bytes(safe_value.value);
                    MONAD_COMPILER_DEBUG_ASSERT(byte_size <= 32);

                    program.push_back(
                        PUSH0 + static_cast<std::uint8_t>(byte_size));

                    auto const *bs = intx::as_bytes(safe_value.value);
                    for (auto i = 0u; i < byte_size; ++i) {
                        program.push_back(bs[byte_size - 1 - i]);
                    }

                    program.push_back(SWAP1 + mem_op);
                    program.push_back(POP);
                });
            }

            program.push_back(op);
        };

        for (auto const &inst : block) {
            std::visit(
                utils::Cases{
                    [&](NonTerminator const &nt) { push_op(nt.opcode); },
                    [&](Terminator const &t) { push_op(t.opcode); },
                    [&](Push const &p) {
                        compile_push(
                            eng, program, p, valid_addresses, jumpdest_patches);
                    },
                },
                inst);
        }
    }

    template <typename Engine>
    void patch_jumpdests(
        Engine &eng, std::vector<std::uint8_t> &program,
        std::vector<std::size_t> const &jumpdest_patches,
        std::vector<std::uint32_t> const &valid_jumpdests)
    {
        if (valid_jumpdests.empty()) {
            return;
        }

        MONAD_COMPILER_DEBUG_ASSERT(std::ranges::is_sorted(jumpdest_patches));
        MONAD_COMPILER_DEBUG_ASSERT(std::ranges::is_sorted(valid_jumpdests));

        // The valid jumpdests and path locations in this program appear in
        // sorted order, so we can bias the generator towards "forwards" jumps
        // in the CFG by simply keeping track of a pointer to the first jumpdest
        // greater than the program offset that we're currently patching, and
        // sampling from that range with greater probability.

        auto forward_jds_begin = valid_jumpdests.begin();
        auto const forward_jds_end = valid_jumpdests.end();

        for (auto const patch : jumpdest_patches) {
            MONAD_COMPILER_DEBUG_ASSERT(patch + 4 < program.size());
            MONAD_COMPILER_DEBUG_ASSERT(program[patch] == PUSH4);

            forward_jds_begin = std::find_if(
                forward_jds_begin, forward_jds_end, [patch](auto jd) {
                    return jd > patch;
                });

            // If there are no possible forwards jumps (i.e. we're in the last
            // block) then we need to unconditionally sample from the full set
            // of jumpdests.
            auto const forward_prob =
                (forward_jds_begin != forward_jds_end) ? 0.8 : 0.0;

            auto const jd = discrete_choice<std::size_t>(
                eng,
                [&](auto &g) { return uniform_sample(g, valid_jumpdests); },
                Choice(forward_prob, [&](auto &g) {
                    return uniform_sample(
                        g, forward_jds_begin, forward_jds_end);
                }));

            auto const *bs = intx::as_bytes(jd);
            for (auto i = 0u; i < 4; ++i) {
                auto &dest = program[patch + i + 1];
                MONAD_COMPILER_DEBUG_ASSERT(dest == 0xFF);

                dest = bs[3 - i];
            }
        }
    }

    template <typename Engine>
    std::vector<std::uint8_t> generate_program(
        Engine &eng, std::vector<evmc::address> const &valid_addresses)
    {
        auto prog = std::vector<std::uint8_t>{};

        auto blocks_dist = std::geometric_distribution(0.1);
        auto const n_blocks = blocks_dist(eng);

        constexpr auto n_exit_blocks = 2;

        auto valid_jumpdests = std::vector<std::uint32_t>{};
        auto jumpdest_patches = std::vector<std::size_t>{};

        for (auto i = 0; i < n_blocks; ++i) {
            auto const is_main = (i == 0);
            auto const is_exit = !is_main && (i <= n_exit_blocks);

            auto const block = generate_block(eng, is_exit, is_main);

            compile_block(
                eng,
                prog,
                block,
                valid_addresses,
                valid_jumpdests,
                jumpdest_patches);
        }

        patch_jumpdests(eng, prog, jumpdest_patches, valid_jumpdests);
        return prog;
    }
}
