add_executable(vm-tests vm_tests.cpp)

monad_compile_options(vm-tests)
target_link_libraries(vm-tests monad-compiler-vm GTest::gtest_main evmc::evmc)
target_include_directories(vm-tests PRIVATE include/)

add_test(
    NAME "VM Tests"
    COMMAND vm-tests
)