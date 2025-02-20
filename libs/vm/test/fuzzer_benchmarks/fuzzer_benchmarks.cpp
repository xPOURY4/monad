#include <monad/fuzzing/generator/generator.hpp>

#include <evmc/evmc.hpp>

#include <benchmark/benchmark.h>

#include <random>

using namespace evmc::literals;

namespace
{
    void benchmark_fuzz_generate(benchmark::State &state)
    {
        auto eng = std::mt19937_64(0);

        for (auto _ : state) {
            auto prog = monad::fuzzing::generate_program(
                eng, {0x0000000000000000000000000000000000001234_address});
            benchmark::DoNotOptimize(prog);
        }
    }
}

BENCHMARK(benchmark_fuzz_generate);

BENCHMARK_MAIN();
