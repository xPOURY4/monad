cmake_minimum_required(VERSION 3.21)

project(monad_trie)

# ###########
# ## deps
# ###########
function(monad_trie_compile_options target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 23)
    set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
endfunction()

include(cmake/test.cmake)

# ###########
# ## libs
# ###########
add_library(monad_trie STATIC
    ${PROJECT_SOURCE_DIR}/include/monad/trie/assert.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/config.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/endian.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/likely.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/offset.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/trie/offset_ptr.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/assert.c
    ${PROJECT_SOURCE_DIR}/src/monad/trie/offset.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/trie/offset_ptr.cpp

    ${PROJECT_SOURCE_DIR}/include/monad/trie/config.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/data.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/find.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/nibble.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/node.h
    ${PROJECT_SOURCE_DIR}/include/monad/trie/update.h
    ${PROJECT_SOURCE_DIR}/src/trie/find.c
    ${PROJECT_SOURCE_DIR}/src/trie/node.c
    ${PROJECT_SOURCE_DIR}/src/trie/update.c
    ${PROJECT_SOURCE_DIR}/src/trie/data.c
)
target_include_directories(monad_trie
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
monad_trie_compile_options(monad_trie)
add_subdirectory(test)
