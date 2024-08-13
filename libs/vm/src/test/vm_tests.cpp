#include <test/fixtures/vm.h>

#include <gtest/gtest.h>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

TEST_F(VMTest, Name)
{
    ASSERT_STREQ(vm.name(), "monad-compiler-vm");
}

TEST_F(VMTest, ABICompatible)
{
    ASSERT_TRUE(vm.is_abi_compatible());
}

TEST_F(VMTest, Capabilities)
{
    ASSERT_TRUE(vm.has_capability(EVMC_CAPABILITY_EVM1));
    ASSERT_FALSE(vm.has_capability(EVMC_CAPABILITY_EWASM));
    ASSERT_FALSE(vm.has_capability(EVMC_CAPABILITY_PRECOMPILES));
}
