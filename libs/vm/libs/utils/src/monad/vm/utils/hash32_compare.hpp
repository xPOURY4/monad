#pragma once

#include <evmc/evmc.hpp>

namespace monad::vm
{
    struct Hash32Compare
    {
        std::size_t hash(evmc::bytes32 const &x) const
        {
            static_assert(sizeof(std::size_t) >= sizeof(uint64_t));
            std::uint64_t result;
            std::memcpy(&result, x.bytes, 8);
            std::uint64_t tmp;
            std::memcpy(&tmp, x.bytes + 8, 8);
            result ^= tmp;
            std::memcpy(&tmp, x.bytes + 16, 8);
            result ^= tmp;
            std::memcpy(&tmp, x.bytes + 24, 8);
            result ^= tmp;
            return result;
        }

        bool equal(evmc::bytes32 const &x, evmc::bytes32 const &y) const
        {
            return x == y;
        }
    };
}
