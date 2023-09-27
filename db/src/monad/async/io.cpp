#include <monad/async/io.hpp>

#include <cassert>

#include <fcntl.h>
#include <poll.h>

MONAD_ASYNC_NAMESPACE_BEGIN

namespace detail
{
    // diseased dead beef in hex, last bit set so won't be a pointer
    static void *const ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC =
        (void *)(uintptr_t)0xd15ea5eddeadbeef;

    struct AsyncIO_per_thread_state_t::within_completions_holder
    {
        AsyncIO_per_thread_state_t *parent;
        within_completions_holder(AsyncIO_per_thread_state_t *_parent)
            : parent(_parent)
        {
            parent->within_completions_count++;
        }
        within_completions_holder(within_completions_holder const &) = delete;
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
    std::pair<int, int> fds, monad::io::Ring &ring_, monad::io::Buffers &rwbuf)
    : owning_tid_(gettid())
    , fds_{fds.first, fds.second, 0, 0}
    , uring_(ring_)
    , rwbuf_(rwbuf)
    , rd_pool_(monad::io::BufferPool(rwbuf, true))
    , wr_pool_(monad::io::BufferPool(rwbuf, false))
{
    _extant_write_operations::init_header(&_extant_write_operations_header);
    MONAD_ASSERT(fds_[READ] != -1);

    auto &ts = detail::AsyncIO_per_thread_state();
    MONAD_ASSERT(ts.instance == nullptr); // currently cannot create more than
                                          // one AsyncIO per thread at a time
    ts.instance = this;

    // register files
    MONAD_ASSERT(!io_uring_register_files(
        const_cast<io_uring *>(&uring_.get_ring()),
        fds_,
        (fds_[WRITE] != -1) ? 2 : 1));

    // create and register the message type pipe for threadsafe communications
    // read side is nonblocking, write side is blocking
    MONAD_ASSERT(::pipe2(fds_ + 2, O_NONBLOCK | O_DIRECT | O_CLOEXEC) != -1);
    MONAD_ASSERT(::fcntl(fds_[MSG_WRITE], F_SETFL, O_DIRECT | O_CLOEXEC) != -1);
    auto *ring = const_cast<io_uring *>(&uring_.get_ring());
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    MONAD_ASSERT(sqe);
    io_uring_prep_poll_multishot(sqe, fds_[MSG_READ], POLLIN);
    io_uring_sqe_set_data(
        sqe, detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC);
    MONAD_ASSERT(io_uring_submit(ring) >= 0);
}

AsyncIO::AsyncIO(
    std::filesystem::path const &p, monad::io::Ring &ring,
    monad::io::Buffers &rwbuf)
    : AsyncIO(
          [&p]() -> std::pair<int, int> {
              int fds[2];
              // append only file descriptor
              fds[WRITE] = ::open(
                  p.c_str(), O_CREAT | O_WRONLY | O_DIRECT | O_CLOEXEC, 0600);
              // read only file descriptor
              fds[READ] = ::open(p.c_str(), O_RDONLY | O_DIRECT | O_CLOEXEC);
              return {fds[0], fds[1]};
          }(),
          ring, rwbuf)
{
}

AsyncIO::AsyncIO(
    use_anonymous_inode_tag, monad::io::Ring &ring, monad::io::Buffers &rwbuf)
    : AsyncIO(
          []() -> std::pair<int, int> {
              int fds[2];
              fds[0] = make_temporary_inode();
              fds[1] = dup(fds[0]);
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

    auto &ts = detail::AsyncIO_per_thread_state();
    MONAD_ASSERT(
        ts.instance ==
        this); // this is being destructed not from its thread, bad idea
    ts.instance = nullptr;

    MONAD_ASSERT(
        !io_uring_unregister_files(const_cast<io_uring *>(&uring_.get_ring())));

    ::close(fds_[READ]);
    if (fds_[WRITE] != -1) {
        ::close(fds_[WRITE]);
    }
    ::close(fds_[MSG_READ]);
    ::close(fds_[MSG_WRITE]);
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
    std::span<std::byte const> buffer, file_offset_t offset, void *uring_data)
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
        // Sleep the thread if there is i/o in flight, as a completion
        // will turn up at some point.
        //
        // Sometimes io_uring_sq_space_left can be zero at the same
        // time as there is no i/o in flight, in this situation don't
        // sleep waiting for completions which will never come.
        _poll_uring(io_in_flight() > 0);
        // Rarely io_uring_sq_space_left stays stuck at zero, almost
        // as if the kernel thread went to sleep or disappeared. This
        // function doesn't do anything if io_uring_sq_space_left is
        // non zero, but if it remains zero then it uses a syscall to
        // give io_uring a poke.
        MONAD_ASSERT(io_uring_sqring_wait(ring) >= 0);
    }
}

bool AsyncIO::_poll_uring(bool blocking)
{
    auto h = detail::AsyncIO_per_thread_state().enter_completions();
    MONAD_DEBUG_ASSERT(owning_tid_ == gettid());

    struct io_uring_cqe *cqe = nullptr;
    auto *const ring = const_cast<io_uring *>(&uring_.get_ring());
    auto inflight_ts = records_.inflight_ts.load(std::memory_order_acquire);

    if (blocking && inflight_ts == 0 &&
        detail::AsyncIO_per_thread_state().empty()) {
        MONAD_ASSERT(!io_uring_wait_cqe(ring, &cqe));
    }
    else {
        if (0 != io_uring_peek_cqe(ring, &cqe) && inflight_ts == 0) {
            return false;
        }
    }

    void *data = (cqe != nullptr)
                     ? io_uring_cqe_get_data(cqe)
                     : detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC;
    MONAD_ASSERT(data);
    erased_connected_operation *state = nullptr;
    result<size_t> res(success(0));
    if (data == detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC) {
        // MSG_READ pipe has a message for us. It is simply the pointer to
        // the connected operation state for us to complete.
        MONAD_ASSERT(cqe == nullptr || cqe->res == POLLIN);
        if (cqe != nullptr && !(cqe->flags & IORING_CQE_F_MORE)) {
            // Rearm the poll
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            MONAD_ASSERT(sqe);
            io_uring_prep_poll_multishot(sqe, fds_[MSG_READ], POLLIN);
            io_uring_sqe_set_data(
                sqe, detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC);
            MONAD_ASSERT(io_uring_submit(ring) >= 0);
        }
        auto readed = ::read(
            fds_[MSG_READ], &state, sizeof(erased_connected_operation *));
        if (readed >= 0) {
            MONAD_ASSERT(sizeof(erased_connected_operation *) == readed);
            // Writes flushed in the submitting thread must be acquired now
            // before state can be dereferenced
            std::atomic_thread_fence(std::memory_order_acquire);
        }
        else {
            if (EAGAIN == errno || EWOULDBLOCK == errno) {
                // Spurious wakeup
                if (cqe != nullptr) {
                    io_uring_cqe_seen(ring, cqe);
                    cqe = nullptr;
                }
                return true;
            }
            else {
                MONAD_ASSERT(readed >= 0);
            }
        }
    }
    else {
        state = reinterpret_cast<erased_connected_operation *>(data);
        res = (cqe->res < 0) ? result<size_t>(posix_code(-cqe->res))
                             : result<size_t>(cqe->res);
    }
    if (cqe != nullptr) {
        io_uring_cqe_seen(ring, cqe);
        cqe = nullptr;
    }

    if (state->is_read()) {
        --records_.inflight_rd;
    }
    else if (state->is_write()) {
        --records_.inflight_wr;
    }
    else if (state->is_timeout()) {
        --records_.inflight_tm;
    }
    else if (state->is_threadsafeop()) {
        records_.inflight_ts.fetch_sub(1, std::memory_order_acq_rel);
    }
    erased_connected_operation_unique_ptr_type h2;
    if (state->lifetime_is_managed_internally()) {
        h2 = erased_connected_operation_unique_ptr_type{state};
    }
    state->completed(std::move(res));
    return true;
}

AsyncIO *AsyncIO::thread_instance() noexcept
{
    auto &ts = detail::AsyncIO_per_thread_state();
    return ts.instance;
}

unsigned AsyncIO::deferred_initiations_in_flight() const noexcept
{
    auto &ts = detail::AsyncIO_per_thread_state();
    return !ts.empty() && !ts.am_within_completions();
}

void AsyncIO::submit_threadsafe_invocation_request(
    erased_connected_operation *uring_data)
{
    // WARNING: This function is usually called from foreign kernel threads!
    records_.inflight_ts.fetch_add(1, std::memory_order_acq_rel);
    // All writes to uring_data must be flushed before doing this
    std::atomic_thread_fence(std::memory_order_release);
    for (;;) {
        auto written =
            ::write(fds_[MSG_WRITE], &uring_data, sizeof(uring_data));
        if (written == sizeof(uring_data)) {
            break;
        }
        MONAD_ASSERT(written == -1);
        MONAD_ASSERT(errno == EINTR);
    }
}

MONAD_ASYNC_NAMESPACE_END
