include_directories(
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
find_package(CLI11 REQUIRED)

# monad trie perf test
add_executable(monad_merge_trie_test
    "monad_merge_trie_test.cpp"
)
monad_compile_options(monad_merge_trie_test)

target_link_libraries(monad_merge_trie_test PUBLIC monad_core CLI11::CLI11 monad_trie ethash::keccak)
