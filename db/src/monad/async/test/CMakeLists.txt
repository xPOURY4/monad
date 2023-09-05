find_package(Boost COMPONENTS fiber REQUIRED)

add_trie_test(sender_receiver_test "sender_receiver.cpp")
target_link_libraries(sender_receiver_test PRIVATE Boost::fiber)

add_trie_test(writeback_cache_test "writeback_cache.cpp")