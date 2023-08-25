#include <monad/async/io.hpp>

#include <fcntl.h>

MONAD_ASYNC_NAMESPACE_BEGIN

AsyncIO::AsyncIO(
    const std::filesystem::path &p, monad::io::Ring &ring,
    monad::io::Buffers &rwbuf)
    : AsyncIO(
          [&p]() -> std::pair<int, int> {
              int fds[2];
              // append only file descriptor
              fds[WRITE] =
                  ::open(p.c_str(), O_CREAT | O_WRONLY | O_DIRECT, 0600);
              // read only file descriptor
              fds[READ] = ::open(p.c_str(), O_RDONLY | O_DIRECT);
              return {fds[0], fds[1]};
          }(),
          ring, rwbuf)
{
}

AsyncIO::~AsyncIO()
{
    wait_until_done();
    MONAD_ASSERT(!records_.inflight);

    MONAD_ASSERT(
        !io_uring_unregister_files(const_cast<io_uring *>(&uring_.get_ring())));

    ::close(fds_[READ]);
    ::close(fds_[WRITE]);
}

void AsyncIO::submit_request(
    std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= READ_BUFFER_SIZE);
#ifndef NDEBUG
    memset(buffer.data(), 0xff, buffer.size());
#endif

    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    io_uring_prep_read_fixed(
        sqe, READ, buffer.data(), buffer.size(), offset, 0);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}
void AsyncIO::submit_request(
    std::span<const std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= WRITE_BUFFER_SIZE);

    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    io_uring_prep_write_fixed(
        sqe, WRITE, buffer.data(), buffer.size(), offset, 1);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}
void AsyncIO::submit_request(timed_invocation_state *state, void *uring_data)
{
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    unsigned flags = 0;
    if (state->timespec_is_absolute) {
        flags |= IORING_TIMEOUT_ABS;
    }
    if (state->timespec_is_utc_clock) {
        flags |= IORING_TIMEOUT_REALTIME;
    }
    io_uring_prep_timeout(sqe, &state->ts, unsigned(-1), flags);

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

bool AsyncIO::poll_uring(bool blocking)
{
    // TODO: handle resource temporarily unavailable error, resubmit the
    // request
    struct io_uring_cqe *cqe = nullptr;

    if (blocking) {
        MONAD_ASSERT(!io_uring_wait_cqe(
            const_cast<io_uring *>(&uring_.get_ring()), &cqe));
    }
    else {
        if (0 != io_uring_peek_cqe(
                     const_cast<io_uring *>(&uring_.get_ring()), &cqe)) {
            return false;
        }
    }

    void *data = io_uring_cqe_get_data(cqe);
    MONAD_ASSERT(data);
    auto *state = reinterpret_cast<erased_connected_operation *>(data);
    result<size_t> res = (cqe->res < 0) ? result<size_t>(posix_code(-cqe->res))
                                        : result<size_t>(cqe->res);
    io_uring_cqe_seen(const_cast<io_uring *>(&uring_.get_ring()), cqe);
    cqe = nullptr;

    --records_.inflight;
    if (state->is_read()) {
        --records_.inflight_rd;
    }
    else if (state->is_write()) {
        --records_.inflight_wr;
    }
    state->completed(std::move(res));
    return true;
}

MONAD_ASYNC_NAMESPACE_END
