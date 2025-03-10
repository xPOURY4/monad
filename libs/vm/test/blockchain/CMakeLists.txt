# Compiler Blockchain Tests

add_executable(compiler-blockchain-tests)
target_sources(compiler-blockchain-tests
    PRIVATE
        compiler_blockchain_tests.cpp
)
target_include_directories(compiler-blockchain-tests
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)
monad_compile_options(compiler-blockchain-tests)

target_link_libraries(compiler-blockchain-tests
    blockchain-test-vm
    utils
    monad-compiler
    monad-compiler-vm
    evmone-blockchaintest-lib)

# Interpreter Blockchain Tests

add_executable(interpreter-blockchain-tests)
target_sources(interpreter-blockchain-tests
    PRIVATE
        interpreter_blockchain_tests.cpp
)
target_include_directories(interpreter-blockchain-tests
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)
monad_compile_options(interpreter-blockchain-tests)

target_link_libraries(interpreter-blockchain-tests
    blockchain-test-vm
    utils
    monad-interpreter
    evmone-blockchaintest-lib)

# Benchmarks

add_executable(blockchain-test-benchmarks)
target_sources(blockchain-test-benchmarks
    PRIVATE
        blockchain_benchmarks.cpp
)
target_include_directories(blockchain-test-benchmarks
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)
monad_compile_options(blockchain-test-benchmarks)

target_link_libraries(blockchain-test-benchmarks
    benchmark::benchmark
    blockchain-test-vm
    utils
    monad-compiler
    monad-compiler-vm
    evmone-blockchaintest-lib)

# Coverage

if(MONAD_COMPILER_COVERAGE)
    target_compile_options(blockchain-test-benchmarks PRIVATE -coverage)
    target_link_options(blockchain-test-benchmarks PRIVATE -coverage)
    target_compile_options(interpreter-blockchain-tests PRIVATE -coverage)
    target_link_options(interpreter-blockchain-tests PRIVATE -coverage)
    target_compile_options(compiler-blockchain-tests PRIVATE -coverage)
    target_link_options(compiler-blockchain-tests PRIVATE -coverage)
endif()
