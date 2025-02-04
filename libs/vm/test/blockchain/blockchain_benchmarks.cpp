#include "test_vm.hpp"

#include <test_resource_data.h>

#include <evmc/evmc.hpp>

#include <blockchaintest.hpp>
#include <evmone/evmone.h>

#include <benchmark/benchmark.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>

using namespace evmone::test;

namespace fs = std::filesystem;

namespace
{
    auto vm_performance_dir = test_resource::ethereum_tests_dir /
                              "BlockchainTests" / "GeneralStateTests" /
                              "VMTests" / "vmPerformance";

    template <bool compile>
    void run_benchmark(benchmark::State &state, fs::path const &json_file)
    {
        if (!fs::is_regular_file(json_file) ||
            json_file.extension() != ".json") {
            state.SkipWithError("Not a JSON test fixture");
            return;
        }

        auto vm = [] {
            if constexpr (compile) {
                return evmc::VM{new BlockchainTestVM};
            }
            else {
                return evmc::VM{evmc_create_evmone()};
            }
        }();

        auto in_file = std::ifstream(json_file);
        auto tests = evmone::test::load_blockchain_tests(in_file);

        for (auto _ : state) {
            evmone::test::run_blockchain_tests(tests, vm);
        }
    }

    auto benchmark_tests()
    {
        return std::array{
            vm_performance_dir / "loopExp.json",
            vm_performance_dir / "loopMul.json",
            vm_performance_dir / "performanceTester.json",
        };
    }

    void register_benchmarks(bool with_evmone)
    {
        for (auto const &test : benchmark_tests()) {
            auto stem = test.stem();

            benchmark::RegisterBenchmark(
                std::format("{}/compiled", stem.string()),
                run_benchmark<true>,
                test);

            if (with_evmone) {
                benchmark::RegisterBenchmark(
                    std::format("{}/evmone", stem.string()),
                    run_benchmark<false>,
                    test);
            }
        }
    }
}

int main(int argc, char **argv)
{
    register_benchmarks(false);

    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
