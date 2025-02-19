add_library(blockchain-test-vm STATIC)
target_sources(blockchain-test-vm
    PRIVATE
        test_vm.hpp
        test_vm.cpp
)
target_link_libraries(blockchain-test-vm
    PUBLIC
        evmc::evmc
        monad-compiler
        monad-compiler-vm
)
target_include_directories(blockchain-test-vm
    PUBLIC .
)
monad_compile_options(blockchain-test-vm)

add_executable(blockchain-tests)
target_sources(blockchain-tests
    PRIVATE
        blockchain_tests.cpp
)
target_include_directories(blockchain-tests
    PRIVATE
        "${TOP_CURRENT_BINARY_DIR}/test"
)
monad_compile_options(blockchain-tests)

target_link_libraries(blockchain-tests
    blockchain-test-vm
    utils
    monad-compiler
    monad-compiler-vm
    evmone-blockchaintest-lib)

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

if(MONAD_COMPILER_COVERAGE)
    target_compile_options(blockchain-tests PRIVATE -coverage)
    target_link_options(blockchain-tests PRIVATE -coverage)
endif()
