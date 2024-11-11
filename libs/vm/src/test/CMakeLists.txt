add_executable(compiler-tests compiler_tests.cpp)

monad_compile_options(compiler-tests)
target_link_libraries(compiler-tests monad-compiler GTest::gtest_main)

add_test(
    NAME "Compiler Tests"
    COMMAND compiler-tests
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
target_link_libraries(uint256-tests monad-compiler GTest::gtest_main)

add_test(
    NAME "UInt256 Tests"
    COMMAND uint256-tests
)

add_executable(strongly-connected-components-tests strongly_connected_components_tests.cpp)

monad_compile_options(strongly-connected-components-tests)
target_link_libraries(strongly-connected-components-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Strongly Connected Components Tests"
    COMMAND strongly-connected-components-tests
)

add_executable(transactional-unordered-map-tests transactional_unordered_map_tests.cpp)

monad_compile_options(transactional-unordered-map-tests)
target_link_libraries(transactional-unordered-map-tests monad-compiler GTest::gtest_main)

add_test(
    NAME "Transactional Unordered Map"
    COMMAND transactional-unordered-map-tests
)

add_executable(subst-map-tests subst_map_tests.cpp)

monad_compile_options(subst-map-tests)
target_link_libraries(subst-map-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Subst Map Tests"
    COMMAND subst-map-tests
)

add_executable(kind-tests kind_tests.cpp)

monad_compile_options(kind-tests)
target_link_libraries(kind-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Kind Tests"
    COMMAND kind-tests
)

add_executable(unify-tests unify_tests.cpp)

monad_compile_options(unify-tests)
target_link_libraries(unify-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Unify Tests"
    COMMAND unify-tests
)

add_executable(infer-tests infer_tests.cpp)

monad_compile_options(infer-tests)
target_link_libraries(infer-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Infer Tests"
    COMMAND infer-tests
)

add_executable(type-check-tests type_check_tests.cpp)

monad_compile_options(type-check-tests)
target_link_libraries(type-check-tests monad-compiler GTest::gtest_main)
add_test(
    NAME "Type Check Tests"
    COMMAND type-check-tests
)