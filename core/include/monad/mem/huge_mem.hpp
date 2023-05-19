#pragma once

#include <monad/config.hpp>

#include <cstddef>

MONAD_NAMESPACE_BEGIN

class HugeMem final
{
    std::size_t const size_;
    unsigned char *const data_;

public:
    HugeMem(std::size_t size);
    ~HugeMem();

    [[gnu::always_inline]] std::size_t get_size() const { return size_; }

    [[gnu::always_inline]] unsigned char *get_data() const { return data_; }
};

static_assert(sizeof(HugeMem) == 16);
static_assert(alignof(HugeMem) == 8);

MONAD_NAMESPACE_END
