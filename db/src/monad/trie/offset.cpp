#include <monad/trie/offset.hpp>

#include <cstdint>

MONAD_TRIE_NAMESPACE_BEGIN

namespace disas
{
    off48_t off48_construct_empty()
    {
        return {};
    }

    void off48_construct_empty(off48_t *const result)
    {
        *result = {};
    }

    off48_t off48_construct_param(int64_t const offset)
    {
        return {offset};
    }

    void off48_construct_param(off48_t *const result, int64_t const offset)
    {
        *result = {offset};
    }

    int64_t off48_cast(off48_t const &offset)
    {
        return offset;
    }
}

MONAD_TRIE_NAMESPACE_END
