cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

option(HUNTER_ENABLED OFF)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MONAD_COMPILER_STANDALONE "Build as a standalone project" ON)

option(MONAD_COMPILER_COVERAGE "Build with coverage" OFF)

include(cmake/MonadCompileOptions.cmake)

find_package(CLI11 REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

set(ROOT_DIRECTORY "${PROJECT_SOURCE_DIR}")
configure_file(cmake/test_resource_data.h.in test/test_resource_data.h @ONLY)
set(TOP_CURRENT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

if(MONAD_COMPILER_STANDALONE)
    set(ETHASH_TESTING NO)
    add_subdirectory(third_party/ethash)

    add_subdirectory(third_party/intx)
    add_subdirectory(third_party/nlohmann_json)

    set(EVMONE_TESTING YES)
    set(EVMONE_RUN_TESTS NO)
    set(EVMC_TESTING NO)
    set(ethash_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake/dummy")
    set(intx_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake/dummy")
    set(nlohmann_json_DIR "${CMAKE_CURRENT_LIST_DIR}/cmake/dummy")
    add_subdirectory(third_party/evmone)
    unset(ethash_DIR)
    unset(intx_DIR)
    unset(nlohmann_json_DIR)
    target_link_libraries(evmone_precompiles PUBLIC ethash::keccak)
    target_link_libraries(evmone-state PUBLIC ethash::keccak intx::intx nlohmann_json::nlohmann_json)
    target_link_libraries(evmone-statetestutils PUBLIC nlohmann_json::nlohmann_json)
    target_link_libraries(evmone PUBLIC ethash::keccak intx::intx)
    target_include_directories(evmone PUBLIC "third_party/evmone/lib")
endif()

add_subdirectory(third_party/asmjit)

add_subdirectory(src/compiler)
add_subdirectory(src/runtime)
add_subdirectory(src/utils)
add_subdirectory(src/vm)
if(MONAD_COMPILER_STANDALONE)
    add_subdirectory(src/test)
endif()

