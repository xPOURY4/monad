cmake_minimum_required(VERSION 3.21)

project(monad_trie)

############
### deps
############

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
    ${PROJECT_SOURCE_DIR}/include/monad/trie/node.h
    ${PROJECT_SOURCE_DIR}/src/trie/node.c
)
target_include_directories(monad_trie
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
monad_trie_compile_options(monad_trie)
