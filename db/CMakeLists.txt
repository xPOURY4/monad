cmake_minimum_required(VERSION 3.21)

project(monad_trie)
enable_testing()

option(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# ##############################################################################
# deps
# ##############################################################################

if(PROJECT_IS_TOP_LEVEL)
  add_subdirectory("monad-core")
endif()

find_package(
  Boost 1.81
  COMPONENTS ""
  REQUIRED)
find_package(GTest)
find_package(
  Boost
  COMPONENTS fiber json
  REQUIRED)
  
# ##############################################################################
# libs
# ##############################################################################
add_library(
  monad_async STATIC
  "${PROJECT_SOURCE_DIR}/src/monad/async/boost_fiber_wrappers.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/async/io.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/async/storage_pool.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/async/util.cpp")
target_include_directories(monad_async PUBLIC "${PROJECT_SOURCE_DIR}/include")
monad_core_compile_options(monad_async)
target_link_libraries(monad_async PUBLIC monad_core Boost::fiber)

add_library(
  monad_trie STATIC
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/config.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/cache_option.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/compute.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/mem.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/nibbles_view.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/nibbles_view_fmt.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/node.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/request.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/traverse.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/trie.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/util.hpp"
  "${PROJECT_SOURCE_DIR}/include/monad/mpt/update.hpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/copy_node.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/find_notify_fiber.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/find.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/mem.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/node.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/traverse.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/trie.cpp"
  "${PROJECT_SOURCE_DIR}/src/monad/mpt/update_aux.cpp")
target_include_directories(monad_trie PUBLIC "${PROJECT_SOURCE_DIR}/include")
monad_core_compile_options(monad_trie)

target_link_libraries(monad_trie PRIVATE Boost::boost)
target_link_libraries(monad_trie PUBLIC monad_async)
target_link_libraries(monad_trie PUBLIC monad_core)

function(add_trie_test)
  set(ONE_VALUE_ARGS TARGET TEST_FILTER)
  set(MULTI_VALUE_ARGS SOURCES LINK_LIBRARIES)
  cmake_parse_arguments(ADD_TRIE_TEST "" "${ONE_VALUE_ARGS}"
                        "${MULTI_VALUE_ARGS}" ${ARGN})

  add_executable(${ADD_TRIE_TEST_TARGET} ${ADD_TRIE_TEST_SOURCES})
  monad_core_compile_options(${ADD_TRIE_TEST_TARGET})
  target_link_libraries(${ADD_TRIE_TEST_TARGET} PUBLIC monad_trie
                                                       GTest::gtest_main
                                                       ${ADD_TRIE_TEST_LINK_LIBRARIES}
  )
  gtest_discover_tests(
    ${ADD_TRIE_TEST_TARGET}
    TEST_PREFIX ${PROJECT_NAME}/ TEST_FILTER ${ADD_TRIE_TEST_TEST_FILTER})
endfunction()

add_subdirectory("src/monad/mpt/test")
add_subdirectory("src/monad/async/test")
add_subdirectory("perf_test")

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_subdirectory("src/monad/mpt/test/fuzz")
endif()
