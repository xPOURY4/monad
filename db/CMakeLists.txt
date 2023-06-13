cmake_minimum_required(VERSION 3.21)

project(monad_trie)

############
### deps
############
include(third_party/ethash/cmake/cable/bootstrap.cmake)
include(CableBuildInfo)

function(monad_trie_compile_options target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 23)
    set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endfunction()


############
### libs
############
add_library(monad_trie STATIC
    ${PROJECT_SOURCE_DIR}/include/monad/mpt/config.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/mpt/update.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/mpt/update_list.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/mpt/update_list.cpp

    ${PROJECT_SOURCE_DIR}/include/monad/trie/util.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/globals.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/constants.hpp
    
    ${PROJECT_SOURCE_DIR}/include/monad/trie/node.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/merge_data.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/node_helper.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/trie.hpp 
    ${PROJECT_SOURCE_DIR}/include/monad/trie/tnode.hpp 
    ${PROJECT_SOURCE_DIR}/include/monad/trie/tr.hpp 
    ${PROJECT_SOURCE_DIR}/include/monad/trie/data.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/merge.hpp   
    ${PROJECT_SOURCE_DIR}/include/monad/trie/io.hpp 
    ${PROJECT_SOURCE_DIR}/include/monad/trie/compact_encode.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/encode_node.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/io.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/node_helper.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/merge.cpp

    ${PROJECT_SOURCE_DIR}/include/monad/trie/tmp_trie.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/tmp_trie.cpp

    ${PROJECT_SOURCE_DIR}/include/monad/trie/config.hpp
)
target_include_directories(monad_trie
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
monad_trie_compile_options(monad_trie)

add_subdirectory(third_party/ethash)
add_subdirectory(monad-core)
target_link_libraries(monad_trie PUBLIC monad_core uring ethash::keccak)

add_subdirectory(merkle_test)
