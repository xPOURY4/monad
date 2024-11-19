#include <runtime/transmute.h>
#include <runtime/types.h>
#include <utils/assert.h>
#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

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
        return intx::be::unsafe::load<intx::uint256>(x.bytes);
    }

    utils::uint256_t uint256_from_address(evmc::address addr)
    {
        auto buf = std::array<std::uint8_t, 32>{};
        std::copy(addr.bytes, addr.bytes + 20, buf.begin());

        return intx::be::unsafe::load<intx::uint256>(buf.data());
    }

    void Environment::set_return_data(
        std::uint8_t const *output_data, std::uint32_t output_size)
    {
        MONAD_COMPILER_ASSERT(return_data.empty());
        return_data = {output_data, output_size};
    }

    void Environment::clear_return_data()
    {
        return_data = {};
    }
}
