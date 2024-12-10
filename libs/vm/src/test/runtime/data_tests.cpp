#include "fixture.h"

#include <runtime/data.h>
#include <runtime/transmute.h>

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <cstdint>
#include <limits>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

constexpr auto addr = utils::uint256_t{678};
constexpr auto wei = utils::uint256_t{782374};

TEST_F(RuntimeTest, BalanceHomestead)
{
    constexpr auto rev = EVMC_HOMESTEAD;
    auto f = wrap(balance<rev>);
    set_balance(addr, wei);

    ctx_.gas_remaining = 0;
    ASSERT_EQ(f(addr), wei);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, BalanceCancunCold)
{
    constexpr auto rev = EVMC_CANCUN;
    auto f = wrap(balance<rev>);
    set_balance(addr, wei);

    ctx_.gas_remaining = 2500;
    ASSERT_EQ(f(addr), wei);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, BalanceCancunWarm)
{
    constexpr auto rev = EVMC_CANCUN;
    auto f = wrap(balance<rev>);
    set_balance(addr, wei);
    host_.access_account(address_from_uint256(addr));

    ctx_.gas_remaining = 0;
    ASSERT_EQ(f(addr), wei);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, CallDataLoadInBounds)
{
    constexpr auto rev = EVMC_CANCUN;
    auto load = wrap(calldataload<rev>);

    ASSERT_EQ(
        load(0),
        0x000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F_u256);

    ASSERT_EQ(
        load(3),
        0x030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122_u256);

    ASSERT_EQ(
        load(96),
        0x606162636465666768696A6B6C6D6E6F707172737475767778797A7B7C7D7E7F_u256);
}

TEST_F(RuntimeTest, CallDataLoadOutOfBounds)
{
    constexpr auto rev = EVMC_CANCUN;
    auto load = wrap(calldataload<rev>);

    ASSERT_EQ(
        call(
            calldataload<EVMC_CANCUN>,
            std::numeric_limits<std::int64_t>::max()),
        0);

    ASSERT_EQ(load(256), 0);

    ASSERT_EQ(
        load(97),
        0x6162636465666768696A6B6C6D6E6F707172737475767778797A7B7C7D7E7F00_u256);

    ASSERT_EQ(
        load(109),
        0x6D6E6F707172737475767778797A7B7C7D7E7F00000000000000000000000000_u256);
}
