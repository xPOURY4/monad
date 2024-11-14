#include <runtime/transmute.h>

#include <utils/uint256.h>

#include <intx/intx.hpp>

#include <evmc/evmc.hpp>

namespace monad::runtime
{
    evmc::bytes32 from_uint256(utils::uint256_t x)
    {
        auto ret = evmc::bytes32{};
        intx::be::unsafe::store(ret.bytes, x);
        return ret;
    }

    utils::uint256_t from_bytes32(evmc::bytes32 x)
    {
        return intx::be::unsafe::load<intx::uint256>(x.bytes);
    }
}
