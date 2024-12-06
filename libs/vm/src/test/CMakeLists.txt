add_subdirectory(utils)

add_executable(unit-tests
    compiler_tests.cpp
    evm_fixture.cpp
    evm_tests.cpp
    utils_tests.cpp
    uint256_tests.cpp
    strongly_connected_components_tests.cpp
    transactional_unordered_map_tests.cpp
    subst_map_tests.cpp
    kind_tests.cpp
    unify_tests.cpp
    infer_tests.cpp
    type_check_tests.cpp
    stack_tests.cpp
    emitter_tests.cpp
    runtime/math_tests.cpp
    runtime/memory_tests.cpp
    runtime/keccak_tests.cpp
)

monad_compile_options(unit-tests)
target_link_libraries(unit-tests
    monad-compiler
    monad-compiler-vm
    utils
    GTest::gtest_main
    evmc::mocked_host
)

add_test(
    NAME "Unit Tests"
    COMMAND unit-tests
)