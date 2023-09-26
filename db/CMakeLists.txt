cmake_minimum_required(VERSION 3.21)

project(monad_trie)
enable_testing()

############
### deps
############
add_subdirectory("monad-core")

include("monad-core/cmake/find_our_dependency.cmake")
find_package(Boost 1.78 COMPONENTS "" REQUIRED)
find_package(GTest)

############
### libs
############
add_library(monad_async STATIC
     "${PROJECT_SOURCE_DIR}/src/monad/async/boost_fiber_wrappers.cpp"
     "${PROJECT_SOURCE_DIR}/src/monad/async/io.cpp"
     "${PROJECT_SOURCE_DIR}/src/monad/async/util.cpp"
)
target_include_directories(monad_async
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
monad_compile_options(monad_async)
target_link_libraries(monad_async PUBLIC monad_core)

add_library(monad_trie STATIC
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/config.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/mem.hpp"
    "${PROJECT_SOURCE_DIR}/src/monad/mpt/mem.cpp"

    "${PROJECT_SOURCE_DIR}/include/monad/mpt/compute.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/nibbles_view.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/node.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/request.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/trie.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/util.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/update.hpp"

    "${PROJECT_SOURCE_DIR}/src/monad/mpt/node.cpp"
    "${PROJECT_SOURCE_DIR}/src/monad/mpt/trie.cpp"

)
target_include_directories(monad_trie
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
monad_compile_options(monad_trie)

target_link_libraries(monad_trie PRIVATE Boost::boost)
target_link_libraries(monad_trie PUBLIC monad_async)
target_link_libraries(monad_trie PUBLIC monad_core)

function(add_trie_test target)
  add_executable(${target} ${ARGN})
  monad_compile_options(${target})
  target_link_libraries(${target} PUBLIC monad_trie GTest::gtest_main)
  gtest_discover_tests(${target} TEST_PREFIX ${PROJECT_NAME}/)
endfunction()

add_subdirectory("src/monad/mpt/test")
add_subdirectory("src/monad/async/test")
add_subdirectory("perf_test")

