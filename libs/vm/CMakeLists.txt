cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/LinkLLVM.cmake)
include(cmake/MonadCompileOptions.cmake)

find_package(LLVM REQUIRED CONFIG)
find_package(CLI11 REQUIRED)

add_subdirectory(third_party/evmc)

add_subdirectory(src/compiler)
add_subdirectory(src/monad-evmc)
add_subdirectory(src/runtime)
add_subdirectory(src/vm)