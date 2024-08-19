add_executable(compiler-tests compiler_tests.cpp)

monad_compile_options(compiler-tests)
target_link_libraries(compiler-tests compiler GTest::gtest_main)

add_test(
    NAME "Compiler Tests"
    COMMAND compiler-tests
)

add_executable(vm-tests vm_tests.cpp simple_execution_tests.cpp)

monad_compile_options(vm-tests)
target_link_libraries(vm-tests monad-compiler-vm GTest::gtest_main evmc::evmc)
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