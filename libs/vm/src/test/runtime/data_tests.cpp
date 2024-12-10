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
