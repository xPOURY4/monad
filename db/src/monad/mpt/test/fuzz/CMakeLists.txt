enable_testing()

find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(monad-trie-fuzztests trie.cpp)
monad_compile_options(monad-trie-fuzztests)
target_link_libraries(monad-trie-fuzztests PUBLIC monad_trie fuzztest_gtest_main)
#fuzztest_setup_fuzzing_flags()
link_fuzztest(monad-trie-fuzztests)
gtest_discover_tests(
    monad-trie-fuzztests PROPERTIES ENVIRONMENT "FUZZTEST_STACK_LIMIT=4194304"
)
