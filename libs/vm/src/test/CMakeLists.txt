add_executable(compiler-tests compiler_tests.cpp)

monad_compile_options(compiler-tests)
target_link_libraries(compiler-tests compiler GTest::gtest_main)

add_test(
    NAME "Compiler Tests"
    COMMAND compiler-tests
)

add_executable(vm-tests vm_tests.cpp simple_execution_tests.cpp)

monad_compile_options(vm-tests)
target_link_libraries(vm-tests PRIVATE monad-compiler-vm GTest::gtest_main evmc::evmc)
target_include_directories(vm-tests PRIVATE include/)

add_test(
    NAME "VM Tests"
    COMMAND vm-tests
)

add_executable(utils-tests utils_tests.cpp)

monad_compile_options(utils-tests)
target_link_libraries(utils-tests utils GTest::gtest_main)

add_test(
    NAME "Utility Tests"
    COMMAND utils-tests
)

add_executable(uint256-tests uint256_tests.cpp)

monad_compile_options(uint256-tests)
target_link_libraries(uint256-tests compiler GTest::gtest_main)

add_test(
    NAME "UInt256 Tests"
    COMMAND uint256-tests
)

add_executable(strongly-connected-components-tests strongly_connected_components_tests.cpp)

monad_compile_options(strongly-connected-components-tests)
target_link_libraries(strongly-connected-components-tests compiler GTest::gtest_main)
target_include_directories(
    strongly-connected-components-tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/compiler/ir
)

add_test(
    NAME "Strongly Connected Components Tests"
    COMMAND strongly-connected-components-tests
)