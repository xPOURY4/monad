#pragma once

#include <monad/mpt/config.hpp>

MONAD_MPT_NAMESPACE_BEGIN

struct StateMachine;

struct DbOptions
{
    StateMachine &machine;
    bool on_disk;
};

MONAD_MPT_NAMESPACE_END
