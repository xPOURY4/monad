#pragma once

namespace monad::utils
{
    template <class... Ts>
    struct Cases : Ts...
    {
        using Ts::operator()...;
    };
}
