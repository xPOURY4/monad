#include <monad/io/ring.hpp>

#include <monad/core/assert.h>

MONAD_IO_NAMESPACE_BEGIN

Ring::Ring(unsigned const entries)
    : ring_{}
{
    int const result = io_uring_queue_init(entries, &ring_, 0);
    MONAD_ASSERT(!result);
}

Ring::~Ring()
{
    io_uring_queue_exit(&ring_);
}

MONAD_IO_NAMESPACE_END
