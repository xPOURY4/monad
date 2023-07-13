function(add_trie_test target)
  add_executable(${target} ${ARGN})
  monad_compile_options(${target})

  target_link_libraries(${target} PUBLIC monad_trie GTest::gtest_main)

  add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
endfunction()

add_trie_test(allocators_test "allocators.cpp")

add_trie_test(
    monad-trie-libtest 
    "index.cpp" 
)
