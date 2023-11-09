find_package(
  Boost
  COMPONENTS fiber
  REQUIRED)

add_trie_test(TARGET boost_fiber_wrappers_test SOURCES
              "boost_fiber_wrappers.cpp")
target_link_libraries(boost_fiber_wrappers_test PRIVATE Boost::fiber)

add_trie_test(TARGET cpp_coroutine_wrappers_test SOURCES
              "cpp_coroutine_wrappers.cpp")

add_trie_test(TARGET io_test SOURCES "io.cpp")

add_trie_test(TARGET io_worker_pool_test SOURCES "io_worker_pool.cpp")

add_trie_test(TARGET sender_receiver_test SOURCES "sender_receiver.cpp"
              TEST_FILTER "-AsyncIO.timed_delay_sender_receiver")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # explodes build times if optimising
  target_compile_options(sender_receiver_test
                         PRIVATE "-fno-var-tracking-assignments")
endif()
target_link_libraries(sender_receiver_test PRIVATE Boost::fiber)

# this test is sensitive to io_uring poll times, so force it to run serially
gtest_discover_tests(
  sender_receiver_test
  TEST_PREFIX ${PROJECT_NAME}/ TEST_FILTER "AsyncIO.timed_delay_sender_receiver"
  PROPERTIES RUN_SERIAL TRUE)

add_trie_test(TARGET storage_pool_test SOURCES "storage_pool.cpp")
