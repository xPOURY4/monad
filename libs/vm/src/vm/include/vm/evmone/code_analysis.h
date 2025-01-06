#pragma once

#include <evmone/baseline.hpp>

#include <evmc/evmc.hpp>

namespace monad
{
    using CodeAnalysis = ::evmone::baseline::CodeAnalysis;

    inline CodeAnalysis analyze(evmc_revision rev, evmc::bytes_view const &code)
    {
        return evmone::baseline::analyze(rev, code);
    }

    inline CodeAnalysis analyze(evmc::bytes_view const &code)
    {
        return analyze(EVMC_FRONTIER, code);
    }
}
