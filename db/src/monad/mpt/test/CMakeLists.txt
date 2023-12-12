add_trie_test(TARGET append_test SOURCES "append_test.cpp")
add_trie_test(TARGET big_endian_serialization_test SOURCES "big_endian_serialization_test.cpp")
add_trie_test(TARGET compaction_test SOURCES "compaction_test.cpp")
add_trie_test(TARGET fiber_future_wrapped_find_test SOURCES
              "fiber_future_wrapped_find.cpp")
add_trie_test(TARGET fiber_future_wrapped_read_test SOURCES
              "fiber_future_wrapped_read.cpp")
add_trie_test(TARGET find_min_max_key_test SOURCES "find_min_max_key_test.cpp")
add_trie_test(TARGET many_nested_updates SOURCES "many_nested_updates.cpp" LINK_LIBRARIES Boost::json)
add_trie_test(TARGET merkle_trie_test SOURCES "merkle_trie_test.cpp")
add_trie_test(TARGET min_truncated_offsets_test SOURCES "min_truncated_offsets_test.cpp")
add_trie_test(TARGET nibbles_view_test SOURCES "nibbles_view_test.cpp")
add_trie_test(TARGET node_test SOURCES "node_test.cpp")
add_trie_test(TARGET plain_trie_test SOURCES "plain_trie_test.cpp")
add_trie_test(TARGET traverse_test SOURCES "traverse_test.cpp")
add_trie_test(TARGET unsigned_20_test SOURCES "unsigned_20_test.cpp")
add_trie_test(TARGET state_machine_test SOURCES "state_machine_test.cpp")

find_package(CLI11 REQUIRED)

# monad trie perf test
add_executable(monad_trie_test "monad_trie_test.cpp")
monad_core_compile_options(monad_trie_test)

target_link_libraries(monad_trie_test PUBLIC monad_core CLI11::CLI11 monad_trie)
