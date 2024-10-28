cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/LinkLLVM.cmake)
include(cmake/MonadCompileOptions.cmake)

find_package(LLVM REQUIRED CONFIG)
find_package(CLI11 REQUIRED)
find_package(GTest REQUIRED)

add_subdirectory(third_party/evmc)
add_subdirectory(third_party/intx)

add_subdirectory(src/compiler)
add_subdirectory(src/monad-evmc)
add_subdirectory(src/runtime)
add_subdirectory(src/test)
add_subdirectory(src/test/fuzz/inference)
add_subdirectory(src/utils)
add_subdirectory(src/vm)
