cmake_minimum_required(VERSION 3.21)

include(CheckCXXSymbolExists)

project(monad_core LANGUAGES C CXX ASM)

option(MONAD_FUZZTEST OFF)

# ##############################################################################
# deps
# ##############################################################################

include("cmake/find_dependency.cmake")
find_dependency(absl REQUIRED)
find_dependency(mimalloc REQUIRED)

set(ETHASH_TESTING NO)
add_subdirectory("third_party/ethash")

set(EXPECTED_BUILD_PACKAGE OFF)
set(EXPECTED_BUILD_TESTS OFF)
add_subdirectory("third_party/expected" SYSTEM)
set(OPTIONAL_BUILD_PACKAGE OFF)
set(OPTIONAL_BUILD_TESTS OFF)
add_subdirectory("third_party/optional" SYSTEM)

add_subdirectory("third_party/nlohmann_json" SYSTEM)

add_subdirectory("third_party/unordered_dense")

if(MONAD_FUZZTEST)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(ANTLR_BUILD_CPP_TESTS OFF)
    add_subdirectory("third_party/fuzztest" SYSTEM)
  endif()
endif()

add_library(
  openssl_sha3_asm INTERFACE
  "third_party/openssl/crypto/sha/asm/keccak1600-avx2.S"
  "third_party/openssl/crypto/sha/asm/keccak1600-avx512.S")
target_include_directories(openssl_sha3_asm INTERFACE "third_party/openssl")

# ##############################################################################
# libs
# ##############################################################################

function(monad_compile_options target)
  set_property(TARGET ${target} PROPERTY C_STANDARD 23)
  set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  target_compile_options(
    ${target} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-missing-field-initializers>)
endfunction()

add_library(
  monad_core STATIC
  "c/include/monad/core/assert.h"
  "c/include/monad/core/bit_util.h"
  "c/include/monad/core/likely.h"
  "c/include/monad/mem/huge_mem.h"
  "c/src/monad/core/assert.c"
  "c/src/monad/mem/huge_mem.c"
  "include/monad/config.hpp"
  # core
  "include/monad/core/byte_string.hpp"
  "include/monad/core/cmemory.hpp"
  "include/monad/core/endian.hpp"
  "include/monad/core/hex_literal.hpp"
  "include/monad/core/keccak.h"
  "include/monad/core/math.hpp"
  "include/monad/core/nibble.h"
  "include/monad/core/offset.hpp"
  "include/monad/core/size_of.hpp"
  "include/monad/core/unaligned.hpp"
  "src/monad/core/assert.cpp"
  "src/monad/core/keccak.c"
  "src/monad/core/keccak_impl.S"
  # io
  "include/monad/io/buffer_pool.hpp"
  "include/monad/io/buffers.hpp"
  "include/monad/io/config.hpp"
  "include/monad/io/ring.hpp"
  "src/monad/io/buffer_pool.cpp"
  "src/monad/io/buffers.cpp"
  "src/monad/io/ring.cpp"
  # mem
  "include/monad/mem/cpool.h"
  "include/monad/mem/huge_mem.hpp"
  "include/monad/mem/dynamic_allocator.hpp"
  "include/monad/mem/mem_map.hpp"
  "include/monad/mem/pool_allocator.hpp"
  "src/monad/mem/huge_mem.cpp"
  "src/monad/mem/mem_map.cpp"
  "src/monad/mem/pool_allocator.cpp"
  # rlp
  "include/monad/rlp/config.hpp"
  "include/monad/rlp/encode.hpp")
target_include_directories(monad_core PUBLIC "include" "c/include")
monad_compile_options(monad_core)
target_link_libraries(monad_core PUBLIC openssl_sha3_asm mimalloc uring)
check_cxx_symbol_exists("_GLIBCXX_HAVE_STACKTRACE" "stacktrace" HAVE_STACKTRACE)
if(HAVE_STACKTRACE)
  target_link_libraries(monad_core PUBLIC stdc++_libbacktrace)
endif()

add_library(
  monad_core_disas SHARED
  "src/monad/core/math_disas.cpp" "src/monad/core/offset_disas.cpp"
  "src/monad/mem/cpool_disas.c" "src/monad/rlp/encode_disas.cpp")
monad_compile_options(monad_core_disas)
target_link_libraries(monad_core_disas PUBLIC monad_core)

# ##############################################################################
# tests
# ##############################################################################

enable_testing()

find_package(GTest REQUIRED)

add_subdirectory("test")

monad_add_test(bit_util_test c/src/monad/core/bit_util_test.cpp)
monad_add_test(huge_mem_test c/src/monad/mem/huge_mem_test.cpp)
