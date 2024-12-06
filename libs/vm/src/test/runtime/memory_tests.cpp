#include "fixture.h"

#include <evmc/evmc.h>

#include <intx/intx.hpp>

#include <runtime/memory.h>

using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

TEST_F(RuntimeTest, EmptyMemory)
{
    auto size = call(msize<EVMC_CANCUN>);
    ASSERT_EQ(size, 0);
    ASSERT_EQ(ctx_.memory_cost, 0);
}

TEST_F(RuntimeTest, MStore)
{
    ctx_.gas_remaining = 6;
    call(mstore<EVMC_CANCUN>, 0, 0xFF);
    ASSERT_EQ(call(msize<EVMC_CANCUN>), 32);
    ASSERT_EQ(ctx_.memory[31], 0xFF);
    ASSERT_EQ(ctx_.memory_cost, 3);
    ASSERT_EQ(ctx_.gas_remaining, 3);

    call(mstore<EVMC_CANCUN>, 1, 0xFF);
    ASSERT_EQ(call(msize<EVMC_CANCUN>), 64);
    ASSERT_EQ(ctx_.memory[31], 0x00);
    ASSERT_EQ(ctx_.memory[32], 0xFF);
    ASSERT_EQ(ctx_.memory_cost, 6);
    ASSERT_EQ(ctx_.gas_remaining, 0);
}

TEST_F(RuntimeTest, MStoreWord)
{
    ctx_.gas_remaining = 3;
    call(
        mstore<EVMC_CANCUN>,
        0,
        0x000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F_u256);

    ASSERT_EQ(call(msize<EVMC_CANCUN>), 32);
    ASSERT_EQ(ctx_.memory_cost, 3);
    ASSERT_EQ(ctx_.gas_remaining, 0);

    for (auto i = 0u; i < 31; ++i) {
        ASSERT_EQ(ctx_.memory[i], i);
    }
}

TEST_F(RuntimeTest, MStore8)
{
    ctx_.gas_remaining = 3;
    call(mstore8<EVMC_CANCUN>, 0, 0xFFFF);
    ASSERT_EQ(ctx_.gas_remaining, 0);
    ASSERT_EQ(ctx_.memory_cost, 3);
    ASSERT_EQ(ctx_.memory[0], 0xFF);
    ASSERT_EQ(ctx_.memory[1], 0x00);

    call(mstore8<EVMC_CANCUN>, 1, 0xFF);
    ASSERT_EQ(ctx_.gas_remaining, 0);
    ASSERT_EQ(ctx_.memory_cost, 3);
    ASSERT_EQ(ctx_.memory[0], 0xFF);
    ASSERT_EQ(ctx_.memory[1], 0xFF);

    ASSERT_EQ(
        call(mload<EVMC_CANCUN>, 0),
        0xFFFF000000000000000000000000000000000000000000000000000000000000_u256);
    ASSERT_EQ(ctx_.gas_remaining, 0);
    ASSERT_EQ(ctx_.memory_cost, 3);
}

TEST_F(RuntimeTest, MLoad)
{
    ctx_.gas_remaining = 6;
    call(mstore<EVMC_CANCUN>, 0, 0xFF);
    ASSERT_EQ(call(mload<EVMC_CANCUN>, 0), 0xFF);
    ASSERT_EQ(ctx_.gas_remaining, 3);
    ASSERT_EQ(ctx_.memory_cost, 3);

    ASSERT_EQ(call(mload<EVMC_CANCUN>, 1), 0xFF00);
    ASSERT_EQ(ctx_.gas_remaining, 0);
    ASSERT_EQ(ctx_.memory_cost, 6);
}

TEST_F(RuntimeTest, QuadraticCosts)
{
    ctx_.gas_remaining = 101;
    ASSERT_EQ(call(mload<EVMC_CANCUN>, 1024), 0);
    ASSERT_EQ(ctx_.gas_remaining, 0);
    ASSERT_EQ(ctx_.memory_cost, 101);
    ASSERT_EQ(call(msize<EVMC_CANCUN>), 1056);
}
