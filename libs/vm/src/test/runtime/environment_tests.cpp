#include "fixture.h"

#include <runtime/environment.h>
#include <runtime/transmute.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <intx/intx.hpp>

using namespace monad::runtime;
using namespace monad::compiler::test;
using namespace intx;

TEST_F(RuntimeTest, Origin)
{
    ASSERT_EQ(
        call(origin<EVMC_CANCUN>),
        uint256_from_address(
            0x000000000000000000000000000000005CA1AB1E_address));
}

TEST_F(RuntimeTest, GasPrice)
{
    ASSERT_EQ(call(gasprice<EVMC_CANCUN>), 56762);
}

TEST_F(RuntimeTest, GasLimit)
{
    ASSERT_EQ(call(gaslimit<EVMC_CANCUN>), 30000000);
}

TEST_F(RuntimeTest, CoinBase)
{
    ASSERT_EQ(
        call(coinbase<EVMC_CANCUN>),
        uint256_from_address(
            0x00000000000000000000000000000000BA5EBA11_address));
}

TEST_F(RuntimeTest, TimeStamp)
{
    ASSERT_EQ(call(timestamp<EVMC_CANCUN>), 1733494490);
}

TEST_F(RuntimeTest, Number)
{
    ASSERT_EQ(call(number<EVMC_CANCUN>), 23784);
}

TEST_F(RuntimeTest, PrevRandao)
{
    ASSERT_EQ(call(prevrandao<EVMC_CANCUN>), 89273);
}

TEST_F(RuntimeTest, ChainId)
{
    ASSERT_EQ(call(chainid<EVMC_CANCUN>), 2342);
}

TEST_F(RuntimeTest, BaseFee)
{
    ASSERT_EQ(call(basefee<EVMC_CANCUN>), 389);
}

TEST_F(RuntimeTest, SelfBalance)
{
    host_.accounts[0x0000000000000000000000000000000000000001_address]
        .set_balance(100);

    ASSERT_EQ(call(selfbalance<EVMC_CANCUN>), 100);
}

TEST_F(RuntimeTest, BlockHashOld)
{
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 1000), 0);
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 23527), 0);
}

TEST_F(RuntimeTest, BlockHashCurrent)
{
    constexpr auto hash =
        0x105DF6064F84551C4100A368056B8AF0E491077245DAB1536D2CFA6AB78421CE_u256;

    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 23528), hash);
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 23660), hash);
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 23783), hash);
}

TEST_F(RuntimeTest, BlockHashNew)
{
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 23784), 0);
    ASSERT_EQ(call(blockhash<EVMC_CANCUN>, 30000), 0);
}
