#pragma once

#include <monad/core/cmemory.hpp>

MONAD_NAMESPACE_BEGIN

template <class T>
constexpr T unaligned_load(const unsigned char *const buf)
{
    T res;
    cmemcpy(reinterpret_cast<unsigned char *>(&res), buf, sizeof(T));
    return res;
}

MONAD_NAMESPACE_END
