add_executable(vm-tests vm_tests.cpp)

monad_compile_options(vm-tests)
target_link_libraries(vm-tests GTest::GTest)

add_test(
    NAME "VM Tests"
    COMMAND vm-tests
)