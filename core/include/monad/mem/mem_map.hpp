#pragma once

#include <monad/config.hpp>

#include <cstddef>

MONAD_NAMESPACE_BEGIN

class MemMap final
{
    size_t const size_;
    unsigned char *const data_;

public:
    MemMap(size_t size, size_t pagesize = 0);
    ~MemMap();

    MemMap(MemMap &&) = default;

    [[gnu::always_inline]] size_t get_size() const
    {
        return size_;
    }

    [[gnu::always_inline]] unsigned char *get_data() const
    {
        return data_;
    }
};

static_assert(sizeof(MemMap) == 16);
static_assert(alignof(MemMap) == 8);

MONAD_NAMESPACE_END
