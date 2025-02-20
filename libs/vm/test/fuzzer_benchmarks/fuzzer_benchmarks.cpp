#include <monad/fuzzing/generator/generator.hpp>

#include <benchmark/benchmark.h>

namespace
{
    void benchmark_fuzz_generate(benchmark::State &state)
    {
        auto eng = std::mt19937_64(0);

        for (auto _ : state) {
            auto prog = monad::fuzzing::generate_program(eng);
            benchmark::DoNotOptimize(prog);
        }
    }
}

BENCHMARK(benchmark_fuzz_generate);

BENCHMARK_MAIN();
