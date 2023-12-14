#include <monad/async/io.hpp>

#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/detail/connected_operation_storage.hpp>
#include <monad/async/detail/scope_polyfill.hpp>
#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/core/unordered_map.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <ostream>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <poll.h>
#include <sys/resource.h> // for setrlimit
#include <unistd.h>

MONAD_ASYNC_NAMESPACE_BEGIN

namespace detail
{
    // diseased dead beef in hex, last bit set so won't be a pointer
    static void *const ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC =
        (void *)(uintptr_t)0xd15ea5eddeadbeef;

    struct AsyncIO_per_thread_state_t::within_completions_holder
    {
        AsyncIO_per_thread_state_t *parent;

        explicit within_completions_holder(AsyncIO_per_thread_state_t *parent_)
            : parent(parent_)
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
        return within_completions_holder{this};
    }

    extern __attribute__((visibility("default"))) AsyncIO_per_thread_state_t &
    AsyncIO_per_thread_state()
    {
        static thread_local AsyncIO_per_thread_state_t v;
        return v;
    }

    static struct AsyncIO_rlimit_raiser_impl
    {
#ifndef NDEBUG
        std::set<int> fd_reservation;
#endif
        AsyncIO_rlimit_raiser_impl()
        {
            size_t n = 4096;
            for (; n >= 1024; n >>= 1) {
                struct rlimit const r{n, n};
                int const ret = setrlimit(RLIMIT_NOFILE, &r);
                if (ret >= 0) {
                    break;
                }
            }
            if (n < 4096) {
                std::cerr << "WARNING: maximum hard file descriptor kimit is "
                          << n
                          << " which is less than 4096. 'Too many open files' "
                             "errors may result. You can increase the hard "
                             "file descriptor limit for a given user by adding "
                             "to '/etc/security/limits.conf' '<username> hard "
                             "nofile 16384'."
                          << std::endl;
            }
#ifndef NDEBUG
            /* If in debug, reserve the first 1024 file descriptor numbers
            in order to better reveal software which is not >= 1024 fd number
            safe, which is still some third party dependencies on Linux. */
            else {
                for (int fd = ::dup(0); fd > 0 && fd < 1024; fd = ::dup(0)) {
                    fd_reservation.insert(fd);
                }
            }
#endif
        }

        ~AsyncIO_rlimit_raiser_impl()
        {
#ifndef NDEBUG
            while (!fd_reservation.empty()) {
                (void)::close(*fd_reservation.begin());
                fd_reservation.erase(fd_reservation.begin());
            }
#endif
        }
    } AsyncIO_rlimit_raiser;

}

AsyncIO::AsyncIO(monad::io::Ring &ring_, monad::io::Buffers &rwbuf)
    : owning_tid_(gettid())
    , fds_{-1, -1}
    , uring_(ring_)
    , rwbuf_(rwbuf)
    , rd_pool_(monad::io::BufferPool(rwbuf, true))
    , wr_pool_(monad::io::BufferPool(rwbuf, false))
{
    extant_write_operations_::init_header(&extant_write_operations_header_);

    auto &ts = detail::AsyncIO_per_thread_state();
    MONAD_ASSERT(ts.instance == nullptr); // currently cannot create more than
                                          // one AsyncIO per thread at a time
    ts.instance = this;

    // create and register the message type pipe for threadsafe communications
    // read side is nonblocking, write side is blocking
    MONAD_ASSERT(
        ::pipe2((int *)&fds_, O_NONBLOCK | O_DIRECT | O_CLOEXEC) != -1);
    MONAD_ASSERT(::fcntl(fds_.msgwrite, F_SETFL, O_DIRECT | O_CLOEXEC) != -1);
    auto *ring = const_cast<io_uring *>(&uring_.get_ring());
    struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
    MONAD_ASSERT(sqe);
    io_uring_prep_poll_multishot(sqe, fds_.msgread, POLLIN);
    io_uring_sqe_set_data(
        sqe, detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC);
    MONAD_ASSERT(io_uring_submit(ring) >= 0);
}

void AsyncIO::init_(std::span<int> fds)
{
    // register files
    for (auto fd : fds) {
        MONAD_ASSERT(fd != -1);
    }
    auto e = io_uring_register_files(
        const_cast<io_uring *>(&uring_.get_ring()),
        fds.data(),
        static_cast<unsigned int>(fds.size()));
    if (e) {
        fprintf(
            stderr,
            "io_uring_register_files failed due to %d %s\n",
            errno,
            strerror(errno));
    }
    MONAD_ASSERT(!e);
}

AsyncIO::AsyncIO(
    class storage_pool &pool, monad::io::Ring &ring, monad::io::Buffers &rwbuf)
    : AsyncIO(ring, rwbuf)
{
    // TODO(niall): In the future don't activate all the chunks, as
    // theoretically zoned storage may enforce a maximum open zone count in
    // hardware. I cannot find any current zoned storage implementation that
    // does not implement infinite open zones so I went ahead and have been lazy
    // here, and we open everything all at once. It also means I can avoid
    // dynamic fd registration with io_uring, which simplifies implementation.
    storage_pool_ = &pool;
    cnv_chunk_ = std::static_pointer_cast<storage_pool::cnv_chunk>(
        pool.activate_chunk(storage_pool::cnv, 0));
    auto count = pool.chunks(storage_pool::seq);
    seq_chunks_.reserve(count);
    std::vector<int> fds;
    fds.reserve(count * 2 + 2);
    fds.push_back(cnv_chunk_.io_uring_read_fd);
    fds.push_back(cnv_chunk_.io_uring_write_fd);
    for (size_t n = 0; n < count; n++) {
        seq_chunks_.emplace_back(
            std::static_pointer_cast<storage_pool::seq_chunk>(
                pool.activate_chunk(
                    storage_pool::seq, static_cast<uint32_t>(n))));
        MONAD_ASSERT(
            seq_chunks_.back().ptr->capacity() >= MONAD_IO_BUFFERS_WRITE_SIZE);
        MONAD_ASSERT(
            (seq_chunks_.back().ptr->capacity() %
             MONAD_IO_BUFFERS_WRITE_SIZE) == 0);
        fds.push_back(seq_chunks_[n].io_uring_read_fd);
        fds.push_back(seq_chunks_[n].io_uring_write_fd);
    }
    /* Annoyingly io_uring refuses duplicate file descriptors in its
    registration, and for efficiency the zoned storage emulation returns the
    same file descriptor for reads (and it may do so for writes depending). So
    reduce to a minimum mapped set.
    */
    unordered_dense_map<int, int> fd_to_iouring_map;
    for (auto fd : fds) {
        MONAD_ASSERT(fd != -1);
        fd_to_iouring_map[fd] = -1;
    }
    int idx = 0;
    fds.clear();
    for (auto &fd : fd_to_iouring_map) {
        fd.second = idx++;
        fds.push_back(fd.first);
    }
    init_(fds);
    auto replace_fds_with_iouring_fds = [&](auto &p) {
        auto it = fd_to_iouring_map.find(p.io_uring_read_fd);
        MONAD_ASSERT(it != fd_to_iouring_map.end());
        p.io_uring_read_fd = it->second;
        it = fd_to_iouring_map.find(p.io_uring_write_fd);
        MONAD_ASSERT(it != fd_to_iouring_map.end());
        p.io_uring_write_fd = it->second;
    };
    replace_fds_with_iouring_fds(cnv_chunk_);
    for (auto &chnk : seq_chunks_) {
        replace_fds_with_iouring_fds(chnk);
    }
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

    ::close(fds_.msgread);
    ::close(fds_.msgwrite);
}

void AsyncIO::submit_request_(
    std::span<std::byte> buffer, chunk_offset_t chunk_and_offset,
    void *uring_data)
{
    assert((chunk_and_offset.offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= READ_BUFFER_SIZE);
#ifndef NDEBUG
    memset(buffer.data(), 0xff, buffer.size());
#endif

    poll_uring_while_submission_queue_full_();
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    auto const &ci = seq_chunks_[chunk_and_offset.id];
    io_uring_prep_read_fixed(
        sqe,
        ci.io_uring_read_fd,
        buffer.data(),
        static_cast<unsigned int>(buffer.size()),
        ci.ptr->read_fd().second + chunk_and_offset.offset,
        0);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

void AsyncIO::submit_request_(
    std::span<std::byte const> buffer, chunk_offset_t chunk_and_offset,
    void *uring_data)
{
    assert((chunk_and_offset.offset & (DISK_PAGE_SIZE - 1)) == 0);
    assert(buffer.size() <= WRITE_BUFFER_SIZE);

    poll_uring_while_submission_queue_full_();
    struct io_uring_sqe *sqe =
        io_uring_get_sqe(const_cast<io_uring *>(&uring_.get_ring()));
    MONAD_ASSERT(sqe);

    auto const &ci = seq_chunks_[chunk_and_offset.id];
    auto offset = ci.ptr->write_fd(buffer.size()).second;
    // Do sanity check to ensure they are appending where they are actually
    // appending
    MONAD_ASSERT((chunk_and_offset.offset & 0xffff) == (offset & 0xffff));
    io_uring_prep_write_fixed(
        sqe,
        ci.io_uring_write_fd,
        buffer.data(),
        static_cast<unsigned int>(buffer.size()),
        offset,
        1);
    sqe->flags |= IOSQE_FIXED_FILE;

    io_uring_sqe_set_data(sqe, uring_data);
    MONAD_ASSERT(
        io_uring_submit(const_cast<io_uring *>(&uring_.get_ring())) >= 0);
}

void AsyncIO::submit_request_(timed_invocation_state *state, void *uring_data)
{
    poll_uring_while_submission_queue_full_();
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

void AsyncIO::poll_uring_while_submission_queue_full_()
{
    auto *ring = const_cast<io_uring *>(&uring_.get_ring());
    // if completions is getting close to full, drain some to prevent
    // completions getting dropped, which would break everything.
    while (io_uring_cq_ready(ring) > (*ring->cq.kring_entries >> 1)) {
        if (!poll_uring_(false)) {
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
        poll_uring_(io_in_flight() > 0);
        // Rarely io_uring_sq_space_left stays stuck at zero, almost
        // as if the kernel thread went to sleep or disappeared. This
        // function doesn't do anything if io_uring_sq_space_left is
        // non zero, but if it remains zero then it uses a syscall to
        // give io_uring a poke.
        MONAD_ASSERT(io_uring_sqring_wait(ring) >= 0);
    }
}

bool AsyncIO::poll_uring_(bool blocking)
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
            io_uring_prep_poll_multishot(sqe, fds_.msgread, POLLIN);
            io_uring_sqe_set_data(
                sqe, detail::ASYNC_IO_MSG_PIPE_READY_IO_URING_DATA_MAGIC);
            MONAD_ASSERT(io_uring_submit(ring) >= 0);
        }
        auto readed =
            ::read(fds_.msgread, &state, sizeof(erased_connected_operation *));
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
        // For now, only silently retry reads
        [[unlikely]] if (
            res.has_error() &&
            res.assume_error() == errc::resource_unavailable_try_again) {
            /* This is what the io_uring source code does when
            EAGAIN comes back in a cqe and the submission queue
            is full. It effectively is a "hard pace", and given how
            rare EAGAIN is, it's probably not a bad idea to truly
            slow things down if it occurs.
            */
            while (io_uring_sq_space_left(ring) == 0) {
                ::usleep(50);
                MONAD_ASSERT(io_uring_sqring_wait(ring) >= 0);
            }
            state->reinitiate();
            return true;
        }
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

unsigned AsyncIO::deferred_initiations_in_flight() const noexcept
{
    auto &ts = detail::AsyncIO_per_thread_state();
    return !ts.empty() && !ts.am_within_completions();
}

void AsyncIO::dump_fd_to(size_t which, std::filesystem::path const &path)
{
    int const tofd = ::creat(path.c_str(), 0600);
    if (tofd == -1) {
        throw std::system_error(std::error_code(errno, std::system_category()));
    }
    auto untodfd = make_scope_exit([tofd]() noexcept { ::close(tofd); });
    auto fromfd = seq_chunks_[which].ptr->read_fd();
    MONAD_ASSERT(fromfd.second <= std::numeric_limits<off64_t>::max());
    off64_t off_in = static_cast<off64_t>(fromfd.second);
    off64_t off_out = 0;
    auto copied = copy_file_range(
        fromfd.first,
        &off_in,
        tofd,
        &off_out,
        seq_chunks_[which].ptr->size(),
        0);
    if (copied == -1) {
        throw std::system_error(std::error_code(errno, std::system_category()));
    }
}

void AsyncIO::submit_threadsafe_invocation_request(
    erased_connected_operation *uring_data)
{
    // WARNING: This function is usually called from foreign kernel threads!
    records_.inflight_ts.fetch_add(1, std::memory_order_acq_rel);
    // All writes to uring_data must be flushed before doing this
    std::atomic_thread_fence(std::memory_order_release);
    for (;;) {
        auto written = ::write(fds_.msgwrite, &uring_data, sizeof(uring_data));
        if (written == sizeof(uring_data)) {
            break;
        }
        MONAD_ASSERT(written == -1);
        MONAD_ASSERT(errno == EINTR);
    }
}

MONAD_ASYNC_NAMESPACE_END
