add_executable(fuzzer-benchmarks)
target_sources(fuzzer-benchmarks
    PRIVATE
        fuzzer_benchmarks.cpp
)
monad_compile_options(fuzzer-benchmarks)

target_link_libraries(fuzzer-benchmarks
    benchmark::benchmark
    monad-fuzzing
)