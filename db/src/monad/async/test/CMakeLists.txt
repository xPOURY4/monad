find_package(Boost COMPONENTS fiber REQUIRED)

add_trie_test(boost_fiber_wrappers_test "boost_fiber_wrappers.cpp")
target_link_libraries(boost_fiber_wrappers_test PRIVATE Boost::fiber)

add_trie_test(cpp_coroutine_wrappers_test "cpp_coroutine_wrappers.cpp")

add_trie_test(io_worker_pool_test "io_worker_pool.cpp")

add_trie_test(sender_receiver_test "sender_receiver.cpp")
target_link_libraries(sender_receiver_test PRIVATE Boost::fiber)

add_trie_test(writeback_cache_test "writeback_cache.cpp")