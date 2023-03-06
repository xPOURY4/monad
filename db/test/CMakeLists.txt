# monad trie unit test
include_directories(
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)

add_library(trie_test_util STATIC
    test_util.h
    test_util.cpp
)
target_link_libraries(trie_test_util
    PUBLIC
    monad_trie
)
monad_trie_compile_options(trie_test_util)

# unit test
add_unit_test(
    TARGET
    trie-test
    SOURCES
    upsert_test.cpp
    erase_test.cpp
    multi_test.cpp
    LIBRARIES
    monad_trie
    trie_test_util)
