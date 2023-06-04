cmake_minimum_required(VERSION 3.21)

project(monad_core)

############
### deps
############

function(monad_compile_options target)
    set_property(TARGET ${target} PROPERTY C_STANDARD 23)
    set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
    set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_compile_options(${target} PRIVATE -Wno-missing-field-initializers)
endfunction()


############
### libs
############

add_library(monad_core STATIC
    ${PROJECT_SOURCE_DIR}/include/monad/config.hpp
    # core
    ${PROJECT_SOURCE_DIR}/include/monad/core/assert.h
    ${PROJECT_SOURCE_DIR}/include/monad/core/byte_string.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/core/likely.h
    ${PROJECT_SOURCE_DIR}/include/monad/core/math.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/core/size_of.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/core/assert.c
    # io
    ${PROJECT_SOURCE_DIR}/include/monad/io/buffer_pool.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/io/buffers.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/io/config.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/io/ring.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/io/buffer_pool.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/io/buffers.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/io/ring.cpp
    # mem
    ${PROJECT_SOURCE_DIR}/include/monad/mem/cpool.h
    ${PROJECT_SOURCE_DIR}/include/monad/mem/huge_mem.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/mem/mem_map.hpp
    ${PROJECT_SOURCE_DIR}/src/monad/mem/huge_mem.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/mem/mem_map.cpp
    # rlp
    ${PROJECT_SOURCE_DIR}/include/monad/rlp/config.hpp
    ${PROJECT_SOURCE_DIR}/include/monad/rlp/encode.hpp
)
target_include_directories(monad_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
monad_compile_options(monad_core)

add_library(monad_core_disas STATIC
    ${PROJECT_SOURCE_DIR}/src/monad/core/math_disas.cpp
    ${PROJECT_SOURCE_DIR}/src/monad/mem/cpool_disas.c
    ${PROJECT_SOURCE_DIR}/src/monad/rlp/encode_disas.cpp
)
monad_compile_options(monad_core_disas)
target_link_libraries(monad_core_disas monad_core)


############
### tests
############

enable_testing()

find_package(GTest REQUIRED)

function(monad_add_test target source)
    add_executable(${target} ${source})
    monad_compile_options(${target})
    target_link_libraries(${target} monad_core GTest::GTest GTest::Main)
    add_test(${target} ${target})
endfunction()

monad_add_test(encode_test ${PROJECT_SOURCE_DIR}/src/monad/rlp/encode_test.cpp)
