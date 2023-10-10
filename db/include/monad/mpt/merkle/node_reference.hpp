#pragma once

#include <monad/core/byte_string.hpp>
#include <monad/core/keccak.h>
#include <monad/mpt/config.hpp>
#include <monad/rlp/encode.hpp>

#include <cstdint>
#include <cstring>

MONAD_MPT_NAMESPACE_BEGIN

// return length of noderef
inline unsigned
to_node_reference(byte_string_view rlp, unsigned char *dest) noexcept
{
    if (MONAD_LIKELY(rlp.size() >= 32)) {
        keccak256(rlp.data(), rlp.size(), dest);
        return 32;
    }
    else {
        std::memcpy(dest, rlp.data(), rlp.size());
        return static_cast<unsigned>(rlp.size());
    }
}

MONAD_MPT_NAMESPACE_END