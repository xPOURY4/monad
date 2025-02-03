#include "evmc/evmc.hpp"
#include "fixture.h"
#include "intx/intx.hpp"
#include "utils/uint256.h"

#include <cstdint>
#include <runtime/transmute.h>

#include <gtest/gtest.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;

namespace
{
    evmc::bytes32 get_test_bytes32()
    {
        evmc::bytes32 b;
        for (std::uint8_t i = 0; i < 32; ++i) {
            b.bytes[31 - i] = i + 1;
        }
        return b;
    }

    evmc::address get_test_address()
    {
        evmc::address b;
        for (std::uint8_t i = 0; i < 20; ++i) {
            b.bytes[19 - i] = i + 1;
        }
        return b;
    }

    utils::uint256_t get_test_uint256()
    {
        utils::uint256_t u;
        uint8_t *b = intx::as_bytes(u);
        for (std::uint8_t i = 0; i < 32; ++i) {
            b[i] = i + 1;
        }
        return u;
    }
};

TEST_F(RuntimeTest, TransmuteBytes32)
{
    evmc::bytes32 const b = get_test_bytes32();
    utils::uint256_t const u = get_test_uint256();
    ASSERT_EQ(bytes32_from_uint256(u), b);
    ASSERT_EQ(u, uint256_from_bytes32(b));
}

TEST_F(RuntimeTest, TransmuteAddress)
{
    evmc::address const a = get_test_address();
    utils::uint256_t u = get_test_uint256();
    ASSERT_EQ(address_from_uint256(u), a);
    uint8_t *b = intx::as_bytes(u);
    for (auto i = 20; i < 32; ++i) {
        b[i] = 0;
    }
    ASSERT_EQ(u, uint256_from_address(a));
}

TEST_F(RuntimeTest, TransmuteBounded)
{
    alignas(8) std::uint8_t buf[33];
    std::uint8_t *input = &buf[0] + 1; // unaligned
    for (std::uint8_t i = 0; i < 32; ++i) {
        input[i] = i + 1;
    }
    for (std::uint8_t len = 0; len <= 32; ++len) {
        utils::uint256_t u{};
        uint8_t *b = intx::as_bytes(u);
        for (std::uint8_t i = 0; i < len; ++i) {
            b[31 - i] = i + 1;
        }
        ASSERT_EQ(uint256_load_bounded_be(input, len), u);
    }
}
