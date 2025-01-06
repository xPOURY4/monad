#include "evm_fixture.h"

#include <compiler/evmone/baseline_execute.h>
#include <compiler/evmone/code_analysis.h>

#include <utils/assert.h>

#include <evmc/bytes.hpp>
#include <evmc/evmc.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <span>
#include <utility>

namespace monad::compiler::test
{
    void EvmTest::pre_execute(
        std::int64_t gas_limit, std::span<std::uint8_t const> calldata) noexcept
    {
        result_ = evmc::Result();
        output_data_ = {};

        msg_.gas = gas_limit;
        msg_.input_data = calldata.data();
        msg_.input_size = calldata.size();

        if (rev_ >= EVMC_BERLIN) {
            host_.access_account(msg_.sender);
            host_.access_account(msg_.recipient);
        }
    }

    void EvmTest::execute(
        std::int64_t gas_limit, std::span<std::uint8_t const> code,
        std::span<std::uint8_t const> calldata, Implementation impl) noexcept
    {
        pre_execute(gas_limit, calldata);

        if (impl == Compiler) {
            result_ = evmc::Result(vm_.compile_and_execute(
                &host_.get_interface(),
                host_.to_context(),
                rev_,
                &msg_,
                code.data(),
                code.size()));
        }
        else {
            MONAD_COMPILER_ASSERT(impl == Evmone);

            result_ = monad::baseline_execute(
                msg_, rev_, &host_, monad::analyze(evmc::bytes_view(code)));
        }
    }

    void EvmTest::execute(
        std::int64_t gas_limit, std::initializer_list<std::uint8_t> code,
        std::span<std::uint8_t const> calldata, Implementation impl) noexcept
    {
        execute(gas_limit, std::span{code}, calldata, impl);
    }

    void EvmTest::execute(
        std::span<std::uint8_t const> code,
        std::span<std::uint8_t const> calldata, Implementation impl) noexcept
    {
        execute(std::numeric_limits<std::int64_t>::max(), code, calldata, impl);
    }

    void EvmTest::execute(
        std::initializer_list<std::uint8_t> code,
        std::span<std::uint8_t const> calldata, Implementation impl) noexcept
    {
        execute(std::span{code}, calldata, impl);
    }

    void EvmTest::execute_and_compare(
        std::int64_t gas_limit, std::initializer_list<std::uint8_t> code,
        std::span<std::uint8_t const> calldata) noexcept
    {
        execute(gas_limit, code, calldata, Compiler);
        auto compiler_result = std::move(result_);

        execute(gas_limit, code, calldata, Evmone);
        auto evmone_result = std::move(result_);

        ASSERT_EQ(compiler_result.status_code, evmone_result.status_code);
        ASSERT_EQ(compiler_result.gas_left, evmone_result.gas_left);
        ASSERT_EQ(compiler_result.gas_refund, evmone_result.gas_refund);
        ASSERT_EQ(compiler_result.output_size, evmone_result.output_size);
        ASSERT_TRUE(std::equal(
            compiler_result.output_data,
            compiler_result.output_data + compiler_result.output_size,
            evmone_result.output_data));
        ASSERT_EQ(
            evmc::address(compiler_result.create_address),
            evmc::address(evmone_result.create_address));
    }
}
