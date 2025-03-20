cmake_minimum_required(VERSION 3.20)

project(monad-compiler)

option(HUNTER_ENABLED OFF)

include(CTest)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MONAD_COMPILER_STANDALONE "Build as a standalone project" ON)
option(MONAD_COMPILER_COVERAGE "Build with coverage" OFF)
option(MONAD_COMPILER_TESTING "Build compiler tests" ON)

option(MONAD_COMPILER_FUZZING "Build compiler and evmone with SAVE_EVM_STACK_ON_EXIT" OFF)

include(cmake/MonadCompileOptions.cmake)

find_package(CLI11 REQUIRED)

if(MONAD_COMPILER_TESTING)
    find_package(GTest REQUIRED)
    find_package(benchmark REQUIRED)
endif()

set(ROOT_DIRECTORY "${PROJECT_SOURCE_DIR}")
configure_file(cmake/test_resource_data.h.in test/test_resource_data.h @ONLY)
set(TOP_CURRENT_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

if(MONAD_COMPILER_STANDALONE)
    set(ETHASH_TESTING NO)
    add_subdirectory(third_party/ethash)

    add_subdirectory(third_party/intx)
    add_subdirectory(third_party/nlohmann_json)
endif()

if (MONAD_COMPILER_FUZZING)
  add_definitions(-DSAVE_EVM_STACK_ON_EXIT)
endif()


# evmone
set(BUILD_SHARED_LIBS OFF)
set(EVMONE_TESTING "${MONAD_COMPILER_TESTING}")
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
target_link_libraries(evmone PUBLIC ethash::keccak intx::intx)
target_include_directories(evmone PUBLIC "third_party/evmone/lib")

# always compile baseline with optimization due to stack size of dispatch_cgoto
set_source_files_properties(
    third_party/evmone/lib/evmone/baseline.cpp
    TARGET_DIRECTORY evmone
    PROPERTIES
        COMPILE_OPTIONS -O2
)

if(MONAD_COMPILER_TESTING)
    target_link_libraries(evmone-state PUBLIC ethash::keccak intx::intx nlohmann_json::nlohmann_json)
    target_link_libraries(evmone-statetestutils PUBLIC nlohmann_json::nlohmann_json)
endif()

# asmjit
add_subdirectory(third_party/asmjit)

add_subdirectory(libs/compiler)
add_subdirectory(libs/evm)
add_subdirectory(libs/fuzzing)
add_subdirectory(libs/interpreter)
add_subdirectory(libs/runtime)
add_subdirectory(libs/utils)
add_subdirectory(libs/vm)
add_subdirectory(cmd/parser)
add_subdirectory(cmd/mce)

if(MONAD_COMPILER_TESTING)
    add_subdirectory(test)
endif()
