#include "evm_fixture.h"

#include <evmc/evmc.h>

#include <cstdint>
#include <initializer_list>
#include <span>

namespace monad::compiler::test
{
    void EvmTest::execute(
        std::int64_t gas_limit, std::span<std::uint8_t const> code,
        std::span<std::uint8_t const> calldata) noexcept
    {
        msg_.gas = gas_limit;
        msg_.input_data = calldata.data();
        msg_.input_size = calldata.size();

        if (rev_ >= EVMC_BERLIN) {
            host_.access_account(msg_.sender);
            host_.access_account(msg_.recipient);
        }

        result_ = evmc::Result(vm_.execute(
            &host_.get_interface(),
            host_.to_context(),
            rev_,
            &msg_,
            code.data(),
            code.size()));
    }

    void EvmTest::execute(
        std::int64_t gas_limit, std::initializer_list<std::uint8_t> code,
        std::span<std::uint8_t const> calldata) noexcept
    {
        execute(gas_limit, std::span{code}, calldata);
    }
}
