#include <monad/vm/evm/opcodes.hpp>
#include <monad/vm/runtime/uint256.hpp>
#include <monad/vm/utils/evm-as/builder.hpp>
#include <monad/vm/utils/evm-as/compiler.hpp>
#include <monad/vm/utils/evm-as/validator.hpp>

#include <test_resource_data.h>

#include "evmc/helpers.h"
#include <CLI/CLI.hpp>
#include <evmc/evmc.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct arguments
{
    bool opaque_parameters = true;
    bool epilogue = false;
};

struct emitter_config
{
    bool validate;
    monad::vm::utils::evm_as::mnemonic_config mconfig;
};

static arguments parse_args(int const argc, char **const argv)
{
    auto app = CLI::App("Computational micro kernel generator");
    auto args = arguments{};

    app.add_option(
        "--opaque-parameters",
        args.opaque_parameters,
        std::format(
            "Hide the concrete parameters from the optimizer (default: {})",
            args.opaque_parameters));
    app.add_option(
        "--with-epilogue",
        args.epilogue,
        std::format(
            "Kernels return their respective results (default: {})",
            args.epilogue));

    try {
        app.parse(argc, argv);
    }
    catch (CLI::ParseError const &e) {
        std::exit(app.exit(e));
    }

    return args;
}

namespace fs = std::filesystem;
using namespace monad::vm::utils;
using namespace monad::vm::runtime;
using EvmBuilder = evm_as::EvmBuilder<EVMC_LATEST_STABLE_REVISION>;

namespace monad::vm::utils::evm_as::kernels
{

    struct KernelBuilder : public ::EvmBuilder
    {
        KernelBuilder &loop(
            uint32_t niterations, size_t in, size_t out,
            ::EvmBuilder const &body)
        {
            if (in > 15) {
                throw std::invalid_argument("input stack is too large");
            }
            int64_t const N = -1 * static_cast<int64_t>(niterations);
            comment("Loop iterator initialization").spush(N);
            if (in > 0) {
                for (size_t i = in; i > 0; i--) {
                    swap(i);
                }
            }

            comment("Computational kernel start")
                .jumpdest("LOOP")
                .append(body)
                .comment("Increment iterator");
            if (out > 0) {
                swap(out);
            }

            push(1).add().dup1();
            if (out > 0) {
                swap(out + 1).swap1();
            }

            jumpi("LOOP").comment("Computational kernel end");

            return *this;
        }

        KernelBuilder &prepare_binop_arguments(
            size_t nclones, uint256_t const &a, uint256_t const &b,
            bool opaque_parameters)
        {
            comment(std::format("    Setup arguments: {}", nclones + 1))
                .push(b)
                .push(a);
            if (opaque_parameters) {
                comment("    Opaque parameters: hide the concrete values from "
                        "the optimizer")
                    .push0()
                    .mstore()
                    .push(32)
                    .mstore()
                    .push(32)
                    .mload()
                    .push0()
                    .mload();
            }
            for (size_t i = 0; i < nclones; i++) {
                comment(std::format("{:4}Arguments clone #{}", "", i + 1))
                    .dup2()
                    .dup2();
            }

            return *this;
        }

        KernelBuilder &binop_loop_body(compiler::EvmOpCode binop)
        {
            ins(binop)
                .swap8()
                .swap1()
                .ins(binop)
                .swap6()
                .ins(binop)
                .swap4()
                .swap1()
                .ins(binop)
                .swap2()
                .ins(binop)
                .dup1()
                .dup3()
                .dup5()
                .dup7()
                .dup9();
            return *this;
        }

        KernelBuilder &epilogue()
        {
            comment("=== Epilogue").push0().mstore().push(32).push0().return_();

            return *this;
        }

        KernelBuilder &comment(std::string const &msg)
        {
            return static_cast<KernelBuilder &>(EvmBuilder::comment(msg));
        }
    };

    using parameterized_binop_kernel_t =
        std::function<KernelBuilder(uint32_t, uint256_t, uint256_t)>;

    parameterized_binop_kernel_t binary_op_micro_kernel(
        compiler::EvmOpCode binop, bool opaque_parameters, bool epilogue)
    {
        return
            [=](uint32_t i, uint256_t arg1, uint256_t arg2) -> KernelBuilder {
                KernelBuilder eb;
                eb.comment("=== Prologue")
                    .prepare_binop_arguments(4, arg1, arg2, opaque_parameters)
                    .loop(i, 10, 10, [binop]() {
                        KernelBuilder eb;
                        eb.binop_loop_body(binop);
                        return eb;
                    }());
                if (epilogue) {
                    eb.epilogue();
                }
                else {
                    eb.stop();
                }
                return eb;
            };
    }
}

void emit_kernel(
    emitter_config const &config, EvmBuilder const &eb, std::string_view name)
{
    auto const dirname =
        test_resource::execution_benchmarks_dir / "basic" / name;

    if (config.validate && !evm_as::validate(eb)) {
        std::cerr << "validation error: " << dirname << std::endl;
        abort();
    }

    if (!fs::exists(dirname) && !fs::create_directory(dirname)) {
        std::cerr << "cannot create directory " << dirname << std::endl;
        abort();
    }

    std::ofstream contract(
        dirname / "contract",
        std::ios::out | std::ios::binary | std::ios::trunc);
    evm_as::compile(eb, contract);

    std::ofstream contract_mnemonic(
        dirname / "contract.mevm", std::ios::out | std::ios::trunc);
    evm_as::mcompile(eb, contract_mnemonic, config.mconfig);

    std::ofstream const calldata(
        dirname / "calldata",
        std::ios::out | std::ios::binary | std::ios::trunc);
}

void emit_kernels(arguments const &config)
{
    using namespace monad::vm::utils::evm_as::kernels;
    using namespace monad::vm::compiler;

    emitter_config const em_config{true, {true, true, 32}};

    std::vector<EvmOpCode> const binops = {
        EvmOpCode::ADD,
        EvmOpCode::SUB,
        EvmOpCode::MUL,
        EvmOpCode::DIV,
        EvmOpCode::SDIV,
        EvmOpCode::MOD,
        EvmOpCode::SMOD,
        EvmOpCode::EXP};

    auto const u8_max = std::numeric_limits<uint8_t>::max();
    auto const u16_max = std::numeric_limits<uint16_t>::max();
    auto const u32_max = std::numeric_limits<uint32_t>::max();
    auto const u64_max = std::numeric_limits<uint64_t>::max();
    auto const u128_max = monad::vm::runtime::pow2(128) - 1;
    auto const u256_max = std::numeric_limits<uint256_t>::max();
    std::vector<std::pair<uint256_t, uint256_t>> const parameters = {
        {0, 0},
        {1, 1},
        {u8_max, u16_max},
        {u16_max, monad::vm::runtime::pow2(240)},
        {123456789, 987654321},
        {u64_max, 1},
        {u64_max, 2},
        {u64_max, u32_max},
        {u64_max - 1, u32_max - 1},
        {u128_max, u16_max},
        {u128_max, u16_max - 1},
        {monad::vm::runtime::pow2(255) - 1, monad::vm::runtime::pow2(254)},
        {u256_max, 0}};
    uint32_t const iterations = 1'000'000;

    for (auto binop : binops) {
        size_t i = 0;
        auto const &info = opcode_table<EVMC_LATEST_STABLE_REVISION>[binop];
        for (auto const &[a, b] : parameters) {
            emit_kernel(
                em_config,
                binary_op_micro_kernel(
                    binop, config.opaque_parameters, config.epilogue)(
                    iterations, a, b),
                std::format("binop_{}_{}", info.name, i++));
        }
    }
    emit_kernel(
        em_config,
        binary_op_micro_kernel(
            EvmOpCode::POP, config.opaque_parameters, config.epilogue)(
            iterations, 0, 0),
        "binop_baseline");
}

int main(int const argc, char **const argv)
{
    auto const config = parse_args(argc, argv);
    emit_kernels(config);
    return 0;
}
