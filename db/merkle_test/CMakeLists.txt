# monad trie unit test
include_directories(
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
find_package(CLI11 REQUIRED)


add_executable(mem_trie
    construct_mem_trie.cpp
)
monad_trie_compile_options(mem_trie)

target_link_libraries(mem_trie PUBLIC monad_core monad_trie ethash::keccak)