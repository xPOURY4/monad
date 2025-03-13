#include <monad/utils/assert.h>

#include <test_resource_data.h>

#include <test_vm.hpp>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include "host.hpp"
#include "state.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using namespace evmone::state;
using namespace test_resource;

using enum BlockchainTestVM::Implementation;

struct free_message
{
    static void operator()(evmc_message *msg) noexcept
    {
        if (msg) {
            delete[] msg->input_data;
            delete[] msg->code;
            delete msg;
        }
    }
};

using msg_ptr = std::unique_ptr<evmc_message, free_message>;

struct benchmark_case
{
    std::string name;
    msg_ptr msg;
};

namespace
{
    auto make_benchmark(
        std::string const &name, std::span<std::uint8_t const> code,
        std::span<std::uint8_t const> input)
    {
        auto *code_buffer = new std::uint8_t[code.size()];
        std::copy(code.begin(), code.end(), code_buffer);

        auto *input_buffer = new std::uint8_t[input.size()];
        std::copy(input.begin(), input.end(), input_buffer);

        auto msg = msg_ptr(new evmc_message{
            .kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 150'000'000,
            .recipient = {},
            .sender = {},
            .input_data = input_buffer,
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = code_buffer,
            .code_size = code.size(),
        });

        return benchmark_case{name, std::move(msg)};
    }

    std::vector<std::uint8_t> read_file(fs::path const &path)
    {
        auto in = std::ifstream(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>{}};
    }

    auto load_benchmark(fs::path const &path)
    {
        MONAD_COMPILER_DEBUG_ASSERT(fs::is_directory(path));

        auto const contract_path = path / "contract";
        MONAD_COMPILER_DEBUG_ASSERT(fs::is_regular_file(contract_path));

        auto const calldata_path = path / "calldata";
        MONAD_COMPILER_DEBUG_ASSERT(fs::is_regular_file(calldata_path));

        return make_benchmark(
            path.stem().string(),
            read_file(contract_path),
            read_file(calldata_path));
    }

    void run_benchmark(
        benchmark::State &state, BlockchainTestVM::Implementation const impl,
        evmc_message const msg)
    {
        auto vm = evmc::VM(new BlockchainTestVM(impl));

        auto evm_state = State{};
        auto block = BlockInfo{};
        auto tx = Transaction{};

        auto host = Host(EVMC_CANCUN, vm, evm_state, block, tx);

        auto *vm_ptr =
            reinterpret_cast<BlockchainTestVM *>(vm.get_raw_pointer());
        auto const *interface = &host.get_interface();
        auto *ctx = host.to_context();
        auto const *code = msg.code;
        auto const code_size = msg.code_size;

        for (auto _ : state) {
            auto const result = vm_ptr->execute(
                interface, ctx, EVMC_CANCUN, &msg, code, code_size);

            MONAD_COMPILER_DEBUG_ASSERT(result.status_code == EVMC_SUCCESS);
        }
    }

    constexpr std::string_view
    impl_name(BlockchainTestVM::Implementation const impl) noexcept
    {
        switch (impl) {
        case Interpreter:
            return "interpreter";
        case Compiler:
            return "compiler";
        case Evmone:
            return "evmone";
        }

        std::unreachable();
    }

    void register_benchmark(std::string_view const name, evmc_message const msg)
    {
        for (auto const impl : {Interpreter, Compiler, Evmone}) {
            benchmark::RegisterBenchmark(
                std::format("execute/{}/{}", name, impl_name(impl)),
                run_benchmark,
                impl,
                msg);
        }
    }

    auto benchmarks() noexcept
    {
        auto ret = std::vector<benchmark_case>{};

        for (auto const &p : fs::directory_iterator(execution_benchmarks_dir)) {
            ret.emplace_back(load_benchmark(p));
        }

        return ret;
    }
}

int main(int argc, char **argv)
{
    auto const all_bms = benchmarks();

    for (auto const &bm : all_bms) {
        register_benchmark(bm.name, *bm.msg);
    }

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
