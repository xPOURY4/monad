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
    utils
    monad-compiler-vm
    evmone-blockchaintest-lib)

include("${CMAKE_CURRENT_SOURCE_DIR}/included_tests.cmake")
list(JOIN INCLUDED_BLOCKCHAIN_TESTS ":" FILTER)

gtest_discover_tests(blockchain-tests
    TEST_PREFIX "Blockchain."
    TEST_FILTER "${FILTER}"
)