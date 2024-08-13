#pragma once

#include <vm/vm.h>

#include <gtest/gtest.h>

#include <evmc/evmc.hpp>

class VMTest : public testing::Test
{
protected:
    VMTest()
        : vm(evmc_create_monad_compiler_vm())
    {
    }

    evmc::VM vm;
};
