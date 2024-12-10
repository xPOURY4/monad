#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.h>
#include <evmc/evmc.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace monad::runtime
{
    evmc::bytes32 bytes_from_uint256(utils::uint256_t x)
    {
        auto ret = evmc::bytes32{};
        intx::be::unsafe::store(ret.bytes, x);
        return ret;
    }

    evmc::address address_from_uint256(utils::uint256_t x)
    {
        auto buf = std::array<std::uint8_t, 32>{};
        intx::be::unsafe::store(buf.data(), x);

        auto ret = evmc::address{};
        std::copy(buf.begin() + 12, buf.end(), ret.bytes);

        return ret;
    }

    utils::uint256_t uint256_from_bytes32(evmc::bytes32 x)
    {
        return uint256_from_span(x.bytes);
    }

    utils::uint256_t uint256_from_address(evmc::address addr)
    {
        return uint256_from_span(addr.bytes);
    }

    utils::uint256_t uint256_from_span(std::span<uint8_t const> bytes)
    {
        MONAD_COMPILER_DEBUG_ASSERT(bytes.size() <= 32);

        auto buf = std::array<std::uint8_t, 32>{};
        std::copy(bytes.begin(), bytes.end(), buf.begin());

        return intx::be::unsafe::load<intx::uint256>(buf.data());
    }

    void Context::deduct_gas(std::int64_t gas)
    {
        gas_remaining -= gas;
        if (MONAD_COMPILER_UNLIKELY(gas_remaining < 0)) {
            exit(StatusCode::OutOfGas);
        }
    }

    evmc_tx_context Context::get_tx_context() const
    {
        return host->get_tx_context(context);
    }

    void Environment::set_return_data(
        std::uint8_t const *output_data, std::uint32_t output_size)
    {
        MONAD_COMPILER_ASSERT(return_data_size == 0);
        return_data = output_data;
        return_data_size = output_size;
    }

    void Environment::clear_return_data()
    {
        return_data_size = 0;
    }
}
