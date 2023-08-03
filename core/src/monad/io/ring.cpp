#include <monad/io/ring.hpp>

#include <monad/core/assert.h>
#include <monad/core/running_on_ci.hpp>

MONAD_IO_NAMESPACE_BEGIN

Ring::Ring(unsigned const entries, unsigned const sq_thread_cpu)
    : ring_{}
    , params_{
          .flags = !running_on_ci()
                       ? (IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF)
                       : 0,
          .sq_thread_cpu = sq_thread_cpu,
          .sq_thread_idle = 60 * 1000}
{
    int const result = io_uring_queue_init_params(entries, &ring_, &params_);
    MONAD_ASSERT(!result);
}

Ring::~Ring()
{
    io_uring_queue_exit(&ring_);
}

MONAD_IO_NAMESPACE_END
