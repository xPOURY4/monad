#pragma once

#include <vm/vm.h>

#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <span>

namespace monad::compiler::test
{
    class EvmTest : public testing::Test
    {
    protected:
        EvmTest() noexcept = default;

        monad::compiler::VM vm_ = {};

        evmc_revision rev_ = EVMC_CANCUN;

        evmc_message msg_ = {};

        evmc::MockedHost host_;

        evmc::Result result_;

        std::span<uint8_t const> output_data_ = {};

        void execute(
            std::int64_t gas_limit, std::span<std::uint8_t const> code,
            std::span<std::uint8_t const> calldata = {}) noexcept;

        void execute(
            std::int64_t gas_limit, std::initializer_list<std::uint8_t> code,
            std::span<std::uint8_t const> calldata = {}) noexcept;
    };
}
