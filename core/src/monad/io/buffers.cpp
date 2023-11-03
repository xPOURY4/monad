#include <monad/io/buffers.hpp>

#include <monad/core/assert.h>

#include <monad/io/ring.hpp>
#include <monad/io/config.hpp>

#include <liburing.h>

#include <sys/uio.h>

#include <bit>
#include <cstddef>

MONAD_IO_NAMESPACE_BEGIN

Buffers::Buffers(
    Ring &ring, size_t const read_count, size_t const write_count,
    size_t const read_size, size_t const write_size)
    : ring_{ring}
    , read_bits_{[=] {
        MONAD_ASSERT(std::has_single_bit(read_size));
        MONAD_ASSERT(read_size >= (1UL << 12));
        return static_cast<size_t>(std::countr_zero(read_size));
    }()}
    , write_bits_{[=] {
        MONAD_ASSERT(std::has_single_bit(write_size));
        MONAD_ASSERT(write_size >= (1UL << 12));
        return static_cast<size_t>(std::countr_zero(write_size));
    }()}
    , read_buf_{read_count * read_size}
    , write_buf_{write_count * write_size}
    , read_count_{read_buf_.get_size() / read_size}
    , write_count_{write_buf_.get_size() / write_size}
{
    iovec const iov[2]{
        {.iov_base = read_buf_.get_data(), .iov_len = read_buf_.get_size()},
        {.iov_base = write_buf_.get_data(), .iov_len = write_buf_.get_size()}};
    MONAD_ASSERT(!io_uring_register_buffers(
        const_cast<io_uring *>(&ring_.get_ring()), iov, 2));
}

Buffers::~Buffers()
{
    MONAD_ASSERT(!io_uring_unregister_buffers(
        const_cast<io_uring *>(&ring_.get_ring())));
}

MONAD_IO_NAMESPACE_END
