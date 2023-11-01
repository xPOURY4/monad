#include <monad/io/ring.hpp>

#include <monad/core/assert.h>

#include <monad/core/cmemory.hpp>

MONAD_IO_NAMESPACE_BEGIN

Ring::Ring(unsigned const entries, unsigned const sq_thread_cpu)
    : ring_{}
    , params_{[&] {
        io_uring_params ret;
        cmemset((char *)&ret, char(0), sizeof(ret));
        ret.flags = IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
        ret.sq_thread_cpu = sq_thread_cpu;
        ret.sq_thread_idle = 60 * 1000;
        return ret;
    }()}
{
    int const result = io_uring_queue_init_params(entries, &ring_, &params_);
    MONAD_ASSERT(!result);
}

Ring::~Ring()
{
    io_uring_queue_exit(&ring_);
}

MONAD_IO_NAMESPACE_END
