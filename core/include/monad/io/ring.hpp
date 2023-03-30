#pragma once

#include <monad/io/config.hpp>

#include <liburing.h>

MONAD_IO_NAMESPACE_BEGIN

class Ring final
{
    io_uring ring_;

public:
    Ring();
    ~Ring();
};

static_assert(sizeof(Ring) == 216);
static_assert(alignof(Ring) == 8);

MONAD_IO_NAMESPACE_END
