#include <monad/core/offset.hpp>

#include <cstdint>

MONAD_NAMESPACE_BEGIN

namespace disas
{
    off48_t off48_from_int(int64_t const off)
    {
        return off;
    }

    int64_t off48_to_int(off48_t const off)
    {
        return off;
    }
}

MONAD_NAMESPACE_END
