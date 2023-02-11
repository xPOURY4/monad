cmake_minimum_required(VERSION 3.20)

include(third_party/ethash/cmake/cable/bootstrap.cmake)
include(CableBuildInfo)

project(tkvdb_test)

add_subdirectory(test)
add_subdirectory(third_party/ethash)

