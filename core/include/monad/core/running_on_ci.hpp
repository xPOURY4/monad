#pragma once

#include "../config.hpp"

MONAD_NAMESPACE_BEGIN

namespace detail
{
    extern bool running_on_ci_impl() noexcept;
}

//! \brief True if we are running on CI e.g. within Github Actions.
inline bool running_on_ci() noexcept
{
    static bool v = detail::running_on_ci_impl();
    return v;
}

MONAD_NAMESPACE_END
