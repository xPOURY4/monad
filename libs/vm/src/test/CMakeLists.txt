add_executable(compiler-tests compiler_tests.cpp)

monad_compile_options(compiler-tests)
target_link_libraries(compiler-tests compiler GTest::gtest_main)
target_include_directories(compiler-tests
    PRIVATE ../compiler/include/
    PRIVATE ../../third_party/intx/include/
)

add_test(
    NAME "Compiler Tests"
    COMMAND compiler-tests
)

add_executable(vm-tests vm_tests.cpp)

monad_compile_options(vm-tests)
target_link_libraries(vm-tests monad-compiler-vm GTest::gtest_main evmc::evmc)
target_include_directories(vm-tests PRIVATE include/)

add_test(
    NAME "VM Tests"
    COMMAND vm-tests
)
