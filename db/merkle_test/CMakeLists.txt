# monad trie unit test
include_directories(
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
find_package(CLI11 REQUIRED)

add_library(trie_test_util STATIC
    test_util.h
    test_util.c
)

target_link_libraries(trie_test_util
    PUBLIC
    monad_trie
    ethash::keccak
)


# monad trie perf test
add_executable(monad_merge_trie_test
    monad_merge_trie_test.cpp
)
monad_trie_compile_options(monad_merge_trie_test)

add_executable(mem_trie
    construct_mem_trie.cpp
)
monad_trie_compile_options(mem_trie)

target_link_libraries(monad_merge_trie_test PUBLIC monad_core CLI11::CLI11 monad_trie trie_test_util ethash::keccak)
target_link_libraries(mem_trie PUBLIC monad_core monad_trie ethash::keccak)