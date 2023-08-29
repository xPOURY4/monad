#include <monad/async/io.hpp>

#include <cassert>

#include <fcntl.h>

MONAD_ASYNC_NAMESPACE_BEGIN

namespace detail
{
    struct AsyncIO_per_thread_state_t::within_completions_holder
    {
        AsyncIO_per_thread_state_t *parent;
        within_completions_holder(AsyncIO_per_thread_state_t *_parent)
            : parent(_parent)
        {
            parent->within_completions_count++;
        }
        within_completions_holder(const within_completions_holder &) = delete;
        within_completions_holder(within_completions_holder &&) = default;
        ~within_completions_holder()
        {
            if (0 == --parent->within_completions_count) {
                parent->within_completions_reached_zero();
            }
        }
    };
    AsyncIO_per_thread_state_t::within_completions_holder
    AsyncIO_per_thread_state_t::enter_completions()
    {
        return {this};
    }
    extern __attribute__((visibility("default"))) AsyncIO_per_thread_state_t &
    AsyncIO_per_thread_state()
    {
        static thread_local AsyncIO_per_thread_state_t v;
        return v;
    }

}

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
    MONAD_ASSERT(!records_.inflight_rd);
    MONAD_ASSERT(!records_.inflight_wr);
    MONAD_ASSERT(!records_.inflight_tm);

    MONAD_ASSERT(
        !io_uring_unregister_files(const_cast<io_uring *>(&uring_.get_ring())));

    ::close(fds_[READ]);
    ::close(fds_[WRITE]);
}

void AsyncIO::_submit_request(
    std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= READ_BUFFER_SIZE);
#ifndef NDEBUG
    memset(buffer.data(), 0xff, buffer.size());
#endif

    _poll_uring_while_submission_queue_full();
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
void AsyncIO::_submit_request(
    std::span<const std::byte> buffer, file_offset_t offset, void *uring_data)
{
    // Trap unintentional use of high bit offsets
    MONAD_ASSERT(offset <= (file_offset_t(1) << 48));
    assert((offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= WRITE_BUFFER_SIZE);

    _poll_uring_while_submission_queue_full();
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
void AsyncIO::_submit_request(timed_invocation_state *state, void *uring_data)
{
    _poll_uring_while_submission_queue_full();
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    if (state->ts.tv_sec != 0 || state->ts.tv_nsec != 0) {
        unsigned flags = 0;
        if (state->timespec_is_absolute) {
            flags |= IORING_TIMEOUT_ABS;
        }
        if (state->timespec_is_utc_clock) {
            flags |= IORING_TIMEOUT_REALTIME;
        }
        io_uring_prep_timeout(sqe, &state->ts, unsigned(-1), flags);
    }
    else {
        io_uring_prep_nop(sqe);
    }

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

void AsyncIO::_poll_uring_while_submission_queue_full()
{
    auto *ring = const_cast<io_uring *>(&uring_.get_ring());
    // if completions is getting close to full, drain some to prevent
    // completions getting dropped, which would break everything.
    while (io_uring_cq_ready(ring) > (*ring->cq.kring_entries >> 1)) {
        if (!_poll_uring(false)) {
            break;
        }
    }
    // block if no available sqe
    while (io_uring_sq_space_left(ring) == 0) {
        _poll_uring(true);
    }
}

bool AsyncIO::_poll_uring(bool blocking)
{
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

    if (state->is_read()) {
        --records_.inflight_rd;
    }
    else if (state->is_write()) {
        --records_.inflight_wr;
    }
    else {
        --records_.inflight_tm;
    }
    auto h = detail::AsyncIO_per_thread_state().enter_completions();
    state->completed(std::move(res));
    return true;
}

MONAD_ASYNC_NAMESPACE_END
