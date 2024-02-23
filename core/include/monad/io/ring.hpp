#pragma once

#include <monad/io/config.hpp>

#include <liburing.h>

#include <optional>

MONAD_IO_NAMESPACE_BEGIN

class Ring final
{
    io_uring ring_;
    io_uring_params params_;

public:
    Ring(unsigned entries, std::optional<unsigned> sq_thread_cpu);
    ~Ring();

    Ring(Ring const &) = delete;
    Ring(Ring &&) = delete;
    Ring &operator=(Ring const &) = delete;
    Ring &operator=(Ring &&) = delete;

    [[gnu::always_inline]] io_uring const &get_ring() const
    {
        return ring_;
    }

    [[gnu::always_inline]] unsigned get_sq_entries() const
    {
        return params_.sq_entries;
    }

    [[gnu::always_inline]] unsigned get_cq_entries() const
    {
        return params_.cq_entries;
    }
};

static_assert(sizeof(Ring) == 336);
static_assert(alignof(Ring) == 8);

MONAD_IO_NAMESPACE_END
