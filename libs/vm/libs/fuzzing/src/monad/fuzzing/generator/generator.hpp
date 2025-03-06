#pragma once

#include <monad/fuzzing/generator/choice.hpp>
#include <monad/fuzzing/generator/instruction_data.hpp>
#include <monad/utils/assert.h>
#include <monad/utils/cases.hpp>
#include <monad/utils/uint256.hpp>

#include <evmc/evmc.hpp>

#include <array>
#include <limits>
#include <memory>
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
        static constexpr auto values = std::array<utils::uint256_t, 4>{
            0,
            1,
            intx::exp(utils::uint256_t(2), utils::uint256_t(255)),
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
    Constant negated_power_of_two_constant(Engine &gen)
    {
        return Constant{-power_of_two_constant(gen).value};
    }

    template <std::size_t Bits = 256, typename Engine = void>
    Constant random_constant(Engine &gen)
        requires(Bits % 64 == 0 && Bits > 0 && Bits <= 256)
    {
        static constexpr auto words = Bits / 64;
        auto dist =
            std::uniform_int_distribution<utils::uint256_t::word_type>();

        return Constant{utils::uint256_t{
            words >= 0 ? dist(gen) : 0,
            words >= 1 ? dist(gen) : 0,
            words >= 2 ? dist(gen) : 0,
            words >= 3 ? dist(gen) : 0,
        }};
    }

    template <typename Engine>
    evmc::address random_address(Engine &eng)
    {
        auto ret = evmc::address{};
        auto const value = random_constant<192>(eng);

        auto const *bytes = intx::as_bytes(value.value);
        std::copy_n(bytes, 20, &ret.bytes[0]);

        return ret;
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
            Choice(0.20, [](auto &g) { return power_of_two_constant(g); }),
            Choice(0.05, [](auto &g) {
                return negated_power_of_two_constant(g);
            }));
    }

    template <typename Engine>
    Push generate_calldata_item(Engine &eng)
    {
        return discrete_choice<Push>(
            eng,
            [](auto &g) { return random_constant(g); },
            Choice(0.25, [](auto &) { return ValidAddress{}; }),
            Choice(0.20, [](auto &g) { return meaningful_constant(g); }),
            Choice(0.20, [](auto &g) { return power_of_two_constant(g); }));
    }

    struct Call
    {
        std::uint8_t opcode;
        uint8_t gas_pct;
        uint8_t balance_pct;
        Constant argsOffset;
        Constant argsSize;
        Constant retOffset;
        Constant retSize;
    };

    template <typename Engine>
    Call generate_call(Engine &eng)
    {
        static constexpr auto pcts =
            std::array<uint8_t, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

        return Call{
            .opcode = uniform_sample(eng, call_non_terminators),
            .gas_pct = uniform_sample(eng, pcts),
            .balance_pct = uniform_sample(eng, pcts),
            .argsOffset = memory_constant(eng),
            .argsSize = memory_constant(eng),
            .retOffset = memory_constant(eng),
            .retSize = memory_constant(eng)};
    }

    struct NonTerminator
    {
        std::uint8_t opcode;
    };

    struct Terminator
    {
        std::uint8_t opcode;
    };

    using Instruction = std::variant<NonTerminator, Terminator, Push, Call>;

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
        static constexpr std::size_t max_block_insts = 1000;

        auto program = std::vector<Instruction>{};

        // Parameters chosen based on the initial fuzzer specification. Because
        // we generate pushes using a different method to other non-terminator
        // instructions, we need to weight their generation probability
        // proportionately to the total number of EVM opcodes. This could be
        // changed in the future to reconfigure the number of pushes vs. other
        // instructions.
        static constexpr auto total_non_term_prob = 0.90;
        static constexpr auto push_weight = (32.0 / 148.0);
        static constexpr auto call_weight = (4.0 / 148.0);
        static constexpr auto non_term_weight =
            1.0 - (push_weight + call_weight);

        static constexpr auto push_prob = total_non_term_prob * push_weight;
        static constexpr auto call_prob = total_non_term_prob * call_weight;
        static constexpr auto non_term_prob =
            total_non_term_prob * non_term_weight;

        static constexpr auto random_byte_prob = 0.000001;
        static constexpr auto terminate_prob =
            (1 - total_non_term_prob) - random_byte_prob;

        if (is_main) {
            // Leave a 5% chance to not generate any pushes in the main block.
            with_probability(eng, 0.95, [&](auto &g) {
                // Parameters chosen by eye; roughly 10% chance of 12 or
                // fewer pushes and 95% chance of 24 or fewer. Could be
                // configured to change the characteristics of this
                // distribution.
                auto main_pushes_dist =
                    std::binomial_distribution<std::size_t>(50, 0.35);
                auto const main_initial_pushes = main_pushes_dist(g);

                for (auto i = 0u; i < main_initial_pushes; ++i) {
                    program.push_back(generate_push(g));
                }
            });
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
                Choice(call_prob, [](auto &g) { return generate_call(g); }),
                Choice(terminate_prob, [&](auto &g) {
                    return generate_terminator(g, is_exit);
                }));

            if (auto *term = std::get_if<Terminator>(&next_inst)) {
                terminated = true;

                auto op = term->opcode;

                if (op == JUMP || op == JUMPI) {
                    with_probability(eng, 0.8, [&](auto &) {
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
    void compile_address(
        Engine &eng, std::vector<std::uint8_t> &program,
        std::vector<evmc::address> const &valid_addresses)
    {
        auto const &addr = uniform_sample(eng, valid_addresses);

        program.push_back(PUSH20);
        for (auto b : addr.bytes) {
            program.push_back(b);
        }
    }

    void compile_constant(std::vector<std::uint8_t> &program, Constant const &c)
    {
        program.push_back(PUSH32);

        auto const *bs = intx::as_bytes(c.value);
        for (auto i = 31; i >= 0; --i) {
            program.push_back(bs[i]);
        }
    }

    void compile_percent(std::vector<std::uint8_t> &program, uint8_t pct)
    {
        program.push_back(PUSH1);
        program.push_back(pct);
        program.push_back(MUL);
        program.push_back(PUSH1);
        program.push_back(10);
        program.push_back(DIV);
    }

    template <typename Engine>
    void compile_call(
        Engine &eng, std::vector<std::uint8_t> &program, Call const &call,
        std::vector<evmc::address> const &valid_addresses)
    {
        if (valid_addresses.empty()) {
            return;
        }

        compile_constant(program, call.retSize);
        compile_constant(program, call.retOffset);
        compile_constant(program, call.argsSize);
        compile_constant(program, call.argsOffset);

        if (call.opcode == CALL || call.opcode == CALLCODE) {
            program.push_back(BALANCE);
            compile_percent(program, call.balance_pct);
        }

        compile_address(eng, program, valid_addresses);

        // send some percentage of available gas
        program.push_back(GAS);
        compile_percent(program, call.gas_pct);
        program.push_back(call.opcode);
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
    void compile_push(
        Engine &eng, std::vector<std::uint8_t> &program, Push const &push,
        std::vector<evmc::address> const &valid_addresses)
    {
        auto patches = std::vector<std::size_t>{};
        compile_push(eng, program, push, valid_addresses, patches);
        MONAD_COMPILER_DEBUG_ASSERT(patches.empty());
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
                    [&](Call const &c) {
                        compile_call(eng, program, c, valid_addresses);
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
        auto const n_blocks = 1 + blocks_dist(eng);

        auto exit_blocks_dist = std::uniform_int_distribution(1, n_blocks);
        auto const n_exit_blocks = exit_blocks_dist(eng);

        auto valid_jumpdests = std::vector<std::uint32_t>{};
        auto jumpdest_patches = std::vector<std::size_t>{};

        for (auto i = 0; i < n_blocks; ++i) {
            auto const is_main = (i == 0);
            auto const is_exit = (i > n_blocks - n_exit_blocks);

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

    template <typename Engine, typename LookupFunc>
    auto message_gas(
        Engine &eng, evmc::address const &target,
        std::vector<evmc::address> const &known_addresses,
        LookupFunc address_lookup) noexcept
    {
        using gas_t = decltype(evmc_message::gas);

        auto const base_gas =
            address_lookup(target).size() * known_addresses.size();

        auto factor_dist = std::normal_distribution(8.0);
        auto const factor = std::max(0.0, factor_dist(eng));

        auto const gas = static_cast<double>(base_gas) * factor;

        MONAD_COMPILER_DEBUG_ASSERT(
            gas <= static_cast<double>(std::numeric_limits<gas_t>::max()));
        MONAD_COMPILER_DEBUG_ASSERT(gas >= 0);

        return static_cast<gas_t>(gas);
    }

    struct message_deleter
    {
        static void operator()(evmc_message *msg) noexcept
        {
            if (!msg) {
                return;
            }

            delete[] msg->input_data;
            delete msg;
        }
    };

    using message_ptr = std::unique_ptr<evmc_message, message_deleter>;

    /**
     * Generates and allocates a calldata buffer containing push-like elements.
     *
     * The caller of this function is responsible for deallocating the buffer
     * appropriately (e.g. by assigning it to the `input_data` member of a
     * `message_ptr`, or explicitly via `delete[]`).
     */
    template <typename Engine>
    std::uint8_t const *generate_input_data(
        Engine &eng, std::size_t const size,
        std::vector<evmc::address> const &known_addresses)
    {
        if (size == 0) {
            return nullptr;
        }

        auto data = std::vector<std::uint8_t>();
        data.reserve(size);

        while (data.size() < size) {
            auto const next_item = generate_calldata_item(eng);
            compile_push(eng, data, next_item, known_addresses);
        }

        auto *const return_buf = new std::uint8_t[size];

        MONAD_COMPILER_DEBUG_ASSERT(data.size() >= size);
        std::copy_n(data.begin(), size, &return_buf[0]);

        return return_buf;
    }

    /**
     * Generate a random EVMC message.
     *
     * Returns a managed pointer to a message, rather than the message itself in
     * order that we can control the lifetime of the `input_data` buffer.
     *
     * Additionally, the `lookup :: Address -> Code` argument here is passed as
     * a lambda to decouple the message generator from any particular concrete
     * state representation. The fuzzer implementation is responsible for
     * instantiating this lookup as appropriate.
     */
    template <typename Engine, typename LookupFunc>
    message_ptr generate_message(
        Engine &eng, evmc::address const &target,
        std::vector<evmc::address> const &known_addresses,
        std::vector<evmc::address> const &known_eoas,
        LookupFunc address_lookup) noexcept
    {
        auto const kind = uniform_sample(
            eng, std::array{EVMC_CALL, EVMC_DELEGATECALL, EVMC_CALLCODE});

        auto const flags = discrete_choice<evmc_flags>(
            eng,
            [](auto &) { return static_cast<evmc_flags>(0); },
            Choice(0.02, [](auto &) { return EVMC_STATIC; }));

        auto const depth =
            std::uniform_int_distribution<decltype(evmc_message::depth)>(
                0, 1023)(eng);

        auto const recipient =
            (kind == EVMC_CALL)
                ? target
                : discrete_choice<evmc::address>(
                      eng,
                      [&](auto &g) {
                          return uniform_sample(g, known_addresses);
                      },
                      Choice(0.01, [&](auto &g) { return random_address(g); }));

        auto const eoa_prob = known_eoas.empty() ? 0.0 : 0.5;
        auto const sender = discrete_choice<evmc::address>(
            eng,
            [&](auto &g) { return uniform_sample(g, known_addresses); },
            Choice(eoa_prob, [&](auto &g) {
                return uniform_sample(g, known_eoas);
            }));

        auto const input_size =
            std::uniform_int_distribution<std::size_t>(0, 1024)(eng);
        auto const *input_data =
            generate_input_data(eng, input_size, known_addresses);

        auto const value = discrete_choice<utils::uint256_t>(
            eng, [](auto &) { return 0; }, Choice(0.9, [&](auto &g) {
                return random_constant<128>(g).value;
            }));

        auto const salt = random_constant(eng).value;

        auto const &code = address_lookup(target);

        return message_ptr{new evmc_message{
            .kind = kind,
            .flags = flags,
            .depth = depth,
            .gas = message_gas(eng, recipient, known_addresses, address_lookup),
            .recipient = recipient,
            .sender = sender,
            .input_data = input_data,
            .input_size = input_size,
            .value = intx::be::store<evmc::bytes32>(value),
            .create2_salt = intx::be::store<evmc::bytes32>(salt),
            .code_address = target,
            .code = code.data(),
            .code_size = code.size(),
        }};
    }

}
