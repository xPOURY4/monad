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
endfunction()


############
### libs
############

add_library(monad_core STATIC
    ${PROJECT_SOURCE_DIR}/include/monad/core/assert.h
    ${PROJECT_SOURCE_DIR}/include/monad/core/bit.h
    ${PROJECT_SOURCE_DIR}/include/monad/core/likely.h
    ${PROJECT_SOURCE_DIR}/include/monad/core/math.h
    ${PROJECT_SOURCE_DIR}/include/monad/mem/cpool.h
    ${PROJECT_SOURCE_DIR}/include/monad/mem/huge_mem.h
    ${PROJECT_SOURCE_DIR}/src/monad/core/assert.c
    ${PROJECT_SOURCE_DIR}/src/monad/mem/huge_mem.c
)
target_include_directories(monad_core
    PUBLIC ${PROJECT_SOURCE_DIR}/include
)
monad_compile_options(monad_core)

add_library(monad_core_disas STATIC
    ${PROJECT_SOURCE_DIR}/src/monad/core/bit_disas.c
    ${PROJECT_SOURCE_DIR}/src/monad/core/math_disas.c
    ${PROJECT_SOURCE_DIR}/src/monad/mem/cpool_disas.c
)
monad_compile_options(monad_core_disas)
target_link_libraries(monad_core_disas monad_core)
