cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

option(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/LinkLLVM.cmake)
include(cmake/MonadCompileOptions.cmake)

find_package(LLVM REQUIRED CONFIG)

add_subdirectory(third_party/evmc)

add_subdirectory(src/compiler)
add_subdirectory(src/vm)