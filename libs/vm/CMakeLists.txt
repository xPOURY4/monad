cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MONAD_COMPILER_STANDALONE "Build as a standalone project" ON)

include(cmake/MonadCompileOptions.cmake)

find_package(CLI11 REQUIRED)
find_package(GTest REQUIRED)

if(MONAD_COMPILER_STANDALONE)
    add_subdirectory(third_party/evmc)
    add_subdirectory(third_party/intx)
endif()

add_subdirectory(third_party/asmjit)

add_subdirectory(src/compiler)
add_subdirectory(src/monad-evmc)
add_subdirectory(src/runtime)
add_subdirectory(src/test)
add_subdirectory(src/utils)
add_subdirectory(src/vm)

option(FUZZ_TESTING "Build fuzzing binaries" OFF)

if(FUZZ_TESTING)
    add_subdirectory(src/test/fuzz/inference)
endif()