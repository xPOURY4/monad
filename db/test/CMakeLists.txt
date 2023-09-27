function(add_trie_test target)
  add_executable(${target} ${ARGN})
  monad_compile_options(${target})

  target_link_libraries(${target} PUBLIC monad_trie GTest::gtest_main)

  gtest_discover_tests(${target} TEST_PREFIX ${PROJECT_NAME}/test/)
endfunction()

find_package(Boost COMPONENTS fiber REQUIRED)

add_trie_test(max_disk_node_size_test "max_disk_node_size.cpp")
add_trie_test(index_test "index.cpp" )
add_trie_test(io_worker_pool_test "io_worker_pool.cpp")
add_trie_test(compact_encode_test "compact_encode.cpp")
add_trie_test(sender_receiver_test "sender_receiver.cpp")
target_link_libraries(sender_receiver_test PRIVATE Boost::fiber)
add_trie_test(
    single_trie_test 
    "trie_fixtures.hpp"
    "single_trie.cpp"
)
add_trie_test(writeback_cache_test "writeback_cache.cpp")
