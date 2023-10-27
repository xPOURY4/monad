#pragma once

#include <monad/mpt/config.hpp>

#include <cstdint>

MONAD_MPT_NAMESPACE_BEGIN

enum class CacheOption : uint8_t
{
    CacheAll = 0,
    ApplyLevelBasedCache,
    DisposeAll,
    Invalid
};

MONAD_MPT_NAMESPACE_END