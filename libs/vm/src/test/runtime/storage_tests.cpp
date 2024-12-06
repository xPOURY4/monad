#include "fixture.h"

#include <runtime/storage.h>
#include <utils/uint256.h>

#include <evmc/evmc.h>

using namespace monad;
using namespace monad::runtime;
using namespace monad::compiler::test;

TEST_F(RuntimeTest, TransientStorage)
{
    constexpr utils::uint256_t key = 6732;
    constexpr utils::uint256_t val = 2389;

    ctx_.gas_remaining = 0;

    ASSERT_EQ(call(tload<EVMC_CANCUN>, key), 0);

    call(tstore<EVMC_CANCUN>, key, val);
    ASSERT_EQ(call(tload<EVMC_CANCUN>, key), val);

    call(tstore<EVMC_CANCUN>, key, val + 10);
    ASSERT_EQ(call(tload<EVMC_CANCUN>, key), val + 10);
}
