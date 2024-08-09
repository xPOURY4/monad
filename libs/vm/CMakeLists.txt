cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

option(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(third_party/evmc)

add_subdirectory(src/compiler)