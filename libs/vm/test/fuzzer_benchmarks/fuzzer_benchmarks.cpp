#include <monad/fuzzing/generator/generator.hpp>

#include <benchmark/benchmark.h>

namespace
{
    void benchmark_fuzz_generate(benchmark::State &state)
    {
        for (auto _ : state) {
            auto prog = monad::fuzzing::generate_program();
            benchmark::DoNotOptimize(prog);
        }
    }
}

BENCHMARK(benchmark_fuzz_generate);

BENCHMARK_MAIN();
