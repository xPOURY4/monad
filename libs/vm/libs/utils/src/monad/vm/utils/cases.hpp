#pragma once

namespace monad::vm::utils
{
    template <class... Ts>
    struct Cases : Ts...
    {
        using Ts::operator()...;
    };
}
