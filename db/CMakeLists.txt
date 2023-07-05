cmake_minimum_required(VERSION 3.21)

project(monad_trie)
enable_testing()

############
### deps
############
include("third_party/ethash/cmake/cable/bootstrap.cmake")
include("CableBuildInfo")

add_subdirectory("monad-core")

include("monad-core/cmake/find_dependency.cmake")
find_package(Boost COMPONENTS "" REQUIRED)
find_package(GTest)

############
### libs
############
add_library(monad_trie STATIC
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/config.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/mem.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/node.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/mpt/update.hpp"
    "${PROJECT_SOURCE_DIR}/src/monad/mpt/mem.cpp"
    "${PROJECT_SOURCE_DIR}/src/monad/mpt/update.cpp"

    "${PROJECT_SOURCE_DIR}/include/monad/trie/util.hpp"

    "${PROJECT_SOURCE_DIR}/include/monad/trie/allocators.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/compact_encode.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/encode_node.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/io.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/node.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/node_helper.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/request.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/tnode.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/tr.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/trie.hpp"
    "${PROJECT_SOURCE_DIR}/include/monad/trie/uring_data.hpp"

    "${PROJECT_SOURCE_DIR}/src/monad/trie/io.cpp"
    "${PROJECT_SOURCE_DIR}/src/monad/trie/node_helper.cpp"
    "${PROJECT_SOURCE_DIR}/src/monad/trie/request.cpp"
    "${PROJECT_SOURCE_DIR}/src/monad/trie/trie.cpp"

    "${PROJECT_SOURCE_DIR}/include/monad/trie/config.hpp"
)
target_include_directories(monad_trie
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
monad_compile_options(monad_trie)

add_subdirectory("third_party/ethash")
target_link_libraries(monad_trie PRIVATE Boost::boost ethash::keccak)
target_link_libraries(monad_trie PUBLIC monad_core)

add_library(monad_trie_disas SHARED
    "${PROJECT_SOURCE_DIR}/src/monad/mpt/node_disas.cpp"
)
target_include_directories(monad_trie_disas
    PUBLIC "${PROJECT_SOURCE_DIR}/include"
)
monad_compile_options(monad_trie_disas)
target_link_libraries(monad_trie_disas PUBLIC monad_trie)

add_subdirectory("merkle_test")

add_subdirectory("test")

