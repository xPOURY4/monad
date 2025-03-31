add_executable(burntpix-benchmark)
target_sources(burntpix-benchmark
    PRIVATE
        burntpix-benchmark.cpp
        code_0a743ba7304efcc9e384ece9be7631e2470e401e.hpp
        code_49206861766520746f6f206d7563682074696d65.hpp
        code_c917e98213a05d271adc5d93d2fee6c1f1006f75.hpp
        code_f529c70db0800449ebd81fbc6e4221523a989f05.hpp
)
target_include_directories(burntpix-benchmark
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)
monad_compile_options(burntpix-benchmark)

target_include_directories(burntpix-benchmark
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)

target_link_libraries(burntpix-benchmark
    benchmark::benchmark
    blockchain-test-vm
    evmone::state
    evmone::statetestutils
    evmone-blockchaintest-lib
    nlohmann_json::nlohmann_json
)

# TODO(BSC): fix this via evmone
target_include_directories(burntpix-benchmark
  PRIVATE "${ROOT_DIRECTORY}/third_party/evmone/test/state"
  PRIVATE "${ROOT_DIRECTORY}/third_party/evmone/test/statetest"
)