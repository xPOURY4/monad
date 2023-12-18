cmake_minimum_required(VERSION 3.25)

include(CheckCXXSymbolExists)

project(monad_core LANGUAGES C CXX ASM)

option(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ##############################################################################
# deps
# ##############################################################################

include("cmake/find_our_dependency.cmake")
find_our_dependency(absl REQUIRED)
find_our_dependency(mimalloc REQUIRED)

cmake_host_system_information(RESULT linux_flavour QUERY DISTRIB_ID)
if(linux_flavour MATCHES "debian|ubuntu")
  find_package(Boost REQUIRED COMPONENTS fiber stacktrace_backtrace)
else()
  find_package(Boost REQUIRED COMPONENTS fiber stacktrace_addr2line)
endif()

set(ETHASH_TESTING NO)
add_subdirectory("third_party/ethash")

add_subdirectory("third_party/nanobench")

add_subdirectory("third_party/nlohmann_json" SYSTEM)

add_subdirectory("third_party/unordered_dense")

add_subdirectory("third_party/quill" SYSTEM)

add_library(
  openssl_sha3_asm INTERFACE
  "third_party/openssl/crypto/sha/asm/keccak1600-avx2.S"
  "third_party/openssl/crypto/sha/asm/keccak1600-avx512.S")
target_include_directories(openssl_sha3_asm INTERFACE "third_party/openssl")

# ##############################################################################
# libs
# ##############################################################################

function(monad_core_compile_options target)
  set_property(TARGET ${target} PROPERTY C_STANDARD 23)
  set_property(TARGET ${target} PROPERTY C_STANDARD_REQUIRED ON)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD 23)
  set_property(TARGET ${target} PROPERTY CXX_STANDARD_REQUIRED ON)

  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
                                           -Wconversion -Werror)

  target_compile_options(
    ${target} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wno-missing-field-initializers>)
endfunction()

add_library(
  monad_core STATIC
  "c/include/monad/core/assert.h"
  "c/include/monad/core/bit_util.h"
  "c/include/monad/core/likely.h"
  "c/include/monad/core/spinlock.h"
  "c/include/monad/core/tl_tid.h"
  "c/include/monad/mem/huge_mem.h"
  "c/src/monad/core/assert.c"
  "c/src/monad/core/tl_tid.c"
  "c/src/monad/mem/huge_mem.c"
  # config
  "include/monad/config.hpp"
  # core
  "include/monad/core/array.hpp"
  "include/monad/core/backtrace.hpp"
  "include/monad/core/basic_formatter.hpp"
  "include/monad/core/byte_string.hpp"
  "include/monad/core/cmemory.hpp"
  "include/monad/core/endian.hpp"
  "include/monad/core/hash.hpp"
  "include/monad/core/hex_literal.hpp"
  "include/monad/core/keccak.h"
  "include/monad/core/math.hpp"
  "include/monad/core/nibble.h"
  "include/monad/core/offset.hpp"
  "include/monad/core/result.hpp"
  "include/monad/core/size_of.hpp"
  "include/monad/core/small_prng.hpp"
  "include/monad/core/unaligned.hpp"
  "src/monad/core/backtrace.cpp"
  "src/monad/core/keccak.c"
  "src/monad/core/keccak_impl.S"
  # fiber
  "include/monad/fiber/algorithm.hpp"
  "include/monad/fiber/config.hpp"
  "include/monad/fiber/properties.hpp"
  "include/monad/fiber/shared_mutex.hpp"
  "src/monad/fiber/algorithm.cpp"
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
  "include/monad/rlp/encode.hpp"
  # test
  "include/monad/test/gtest_signal_stacktrace_printer.hpp")
target_include_directories(monad_core PUBLIC "include" "c/include")
monad_core_compile_options(monad_core)
target_compile_definitions(monad_core PUBLIC "BOOST_STACKTRACE_LINK=1")
if(linux_flavour MATCHES "debian|ubuntu")
  target_compile_definitions(monad_core
                             PUBLIC "BOOST_STACKTRACE_USE_BACKTRACE=1")
  target_link_libraries(monad_core PUBLIC Boost::stacktrace_backtrace)
else()
  target_compile_definitions(monad_core
                             PUBLIC "BOOST_STACKTRACE_USE_ADDR2LINE=1")
  target_link_libraries(monad_core PUBLIC Boost::stacktrace_addr2line)
endif()
target_link_libraries(monad_core PUBLIC openssl_sha3_asm mimalloc uring)

add_library(
  monad_core_disas STATIC
  "c/src/monad/core/spinlock_disas.c"
  "src/monad/core/math_disas.cpp" "src/monad/core/offset_disas.cpp"
  "src/monad/mem/cpool_disas.c" "src/monad/rlp/encode_disas.cpp")
monad_core_compile_options(monad_core_disas)
target_link_libraries(monad_core_disas PUBLIC monad_core)

# ##############################################################################
# tests
# ##############################################################################

enable_testing()

find_package(GTest REQUIRED)

add_subdirectory("test")

monad_add_test(bit_util_test c/src/monad/core/bit_util_test.cpp)
monad_add_test(huge_mem_test c/src/monad/mem/huge_mem_test.cpp)
monad_add_test(test_shared_mutex "src/monad/fiber/test_shared_mutex.cpp")
target_link_libraries(test_shared_mutex nanobench Boost::fiber)
