#include <monad/fuzzing/generator/generator.hpp>
#include <monad/utils/cases.hpp>

#include <evmc/evmc.hpp>

#include <random>

using namespace evmc::literals;

namespace monad::fuzzing
{
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

                    auto *bs = intx::as_bytes(c.value);
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
        for (auto const patch : jumpdest_patches) {
            MONAD_COMPILER_DEBUG_ASSERT(patch + 4 < program.size());
            MONAD_COMPILER_DEBUG_ASSERT(program[patch] == PUSH4);

            auto const jd = uniform_sample(eng, valid_jumpdests);
            auto const *bs = intx::as_bytes(jd);

            for (auto i = 0u; i < 4; ++i) {
                auto &dest = program[patch + i + 1];
                MONAD_COMPILER_DEBUG_ASSERT(dest == 0xFF);

                dest = bs[3 - i];
            }
        }
    }

    std::vector<std::uint8_t> generate_program()
    {
        auto rd = std::random_device();
        auto eng = std::mt19937_64(rd());

        auto prog = std::vector<std::uint8_t>{};

        constexpr auto n_blocks = 10;
        constexpr auto exit_blocks = 5;

        auto const valid_addresses = std::vector{
            0x000102030405060708090A0B0C0D0E0F10111213_address,
        };

        auto valid_jumpdests = std::vector<std::uint32_t>{};
        auto jumpdest_patches = std::vector<std::size_t>{};

        for (auto i = 0; i < n_blocks; ++i) {
            auto const is_main = (i == 0);
            auto const is_exit = !is_main && (i <= exit_blocks);

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
