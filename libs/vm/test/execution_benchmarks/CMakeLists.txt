add_executable(execution-benchmarks)
target_sources(execution-benchmarks
    PRIVATE
        benchmarks.cpp
)
monad_compile_options(execution-benchmarks)

target_include_directories(execution-benchmarks
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)

target_link_libraries(execution-benchmarks
    benchmark::benchmark
    blockchain-test-vm
    evmone::state
)

# TODO(BSC): fix this via evmone
target_include_directories(execution-benchmarks
  PRIVATE "${ROOT_DIRECTORY}/third_party/evmone/test/state"
)