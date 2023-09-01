#pragma once

#include <monad/async/connected_operation.hpp>

#include <monad/async/util.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/mem/allocators.hpp>

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <tuple>

MONAD_ASYNC_NAMESPACE_BEGIN

class read_single_buffer_sender;

// helper struct that records IO stats
struct IORecord
{
    unsigned inflight_rd{0};
    unsigned inflight_wr{0};
    unsigned inflight_tm{0};
    std::atomic<unsigned> inflight_ts{0};
    unsigned nreads{0}; // Reads done since last `flush()`
};

class AsyncIO final
{
public:
    struct timed_invocation_state;

private:
    friend class read_single_buffer_sender;
    constexpr static unsigned READ = 0, WRITE = 1, MSG_READ = 2, MSG_WRITE = 3;

    // TODO: using user_data_t = variant<update_data_t, write_data_t>
    int fds_[4];
    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;

    // IO records
    IORecord records_;

    void _submit_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data);
    void _submit_request(
        std::span<const std::byte> buffer, file_offset_t offset,
        void *uring_data);
    void _submit_request(timed_invocation_state *state, void *uring_data);

    void _poll_uring_while_submission_queue_full();
    bool _poll_uring(bool blocking);

public:
    AsyncIO(
        std::pair<int, int> fds, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf);
    AsyncIO(
        const std::filesystem::path &p, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf);
    AsyncIO(
        use_anonymous_inode_tag, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf);
    ~AsyncIO();

    unsigned io_in_flight() const noexcept
    {
        return records_.inflight_rd + records_.inflight_wr +
               records_.inflight_tm +
               records_.inflight_ts.load(std::memory_order_relaxed);
    }

    unsigned reads_in_flight() const noexcept
    {
        return records_.inflight_rd;
    }

    unsigned writes_in_flight() const noexcept
    {
        return records_.inflight_wr;
    }

    unsigned timers_in_flight() const noexcept
    {
        return records_.inflight_tm;
    }

    unsigned threadsafeops_in_flight() const noexcept
    {
        return records_.inflight_ts.load(std::memory_order_relaxed);
    }

    // Blocks until at least one completion is processed, returning number
    // of completions processed.
    size_t poll_blocking(size_t count = 1)
    {
        size_t n = 0;
        for (; n < count; n++) {
            if (!_poll_uring(n == 0)) {
                break;
            }
        }
        return n;
    }

    // Never blocks
    size_t poll_nonblocking(size_t count = size_t(-1))
    {
        size_t n = 0;
        for (; n < count; n++) {
            if (!_poll_uring(false)) {
                break;
            }
        }
        return n;
    }

    void wait_until_done()
    {
        while (io_in_flight() > 0) {
            poll_blocking(size_t(-1));
        }
    }

    void flush()
    {
        wait_until_done();
        records_.nreads = 0;
    }

    void submit_read_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
    {
        _submit_request(buffer, offset, uring_data);
        ++records_.inflight_rd;
        ++records_.nreads;
    }

    constexpr int get_rd_fd() noexcept
    {
        return fds_[READ];
    }

    void submit_write_request(
        std::span<const std::byte> buffer, file_offset_t offset,
        void *uring_data)
    {
        _submit_request(buffer, offset, uring_data);
        ++records_.inflight_wr;
    }

    // WARNING: Must exist until completion!
    struct timed_invocation_state
    {
        struct __kernel_timespec ts
        {
            0, 0
        };
        bool timespec_is_absolute{false};
        bool timespec_is_utc_clock{false};
    };
    void submit_timed_invocation_request(
        timed_invocation_state *info, void *uring_data)
    {
        _submit_request(info, uring_data);
        ++records_.inflight_tm;
    }

    void submit_threadsafe_invocation_request(void *uring_data);

    /* This isn't the ideal place to put this, but only AsyncIO knows how to
    get i/o buffers into which to place connected i/o states.
    */
    static constexpr size_t MAX_CONNECTED_OPERATION_SIZE = DISK_PAGE_SIZE;
    static constexpr size_t READ_BUFFER_SIZE = 7 * DISK_PAGE_SIZE;
    static constexpr size_t WRITE_BUFFER_SIZE =
        8 * 1024 * 1024 - MAX_CONNECTED_OPERATION_SIZE;
    static constexpr size_t MONAD_IO_BUFFERS_READ_SIZE =
        round_up_align<CPU_PAGE_BITS>(
            READ_BUFFER_SIZE + MAX_CONNECTED_OPERATION_SIZE);
    static constexpr size_t MONAD_IO_BUFFERS_WRITE_SIZE =
        round_up_align<CPU_PAGE_BITS>(
            WRITE_BUFFER_SIZE + MAX_CONNECTED_OPERATION_SIZE);
    template <class ConnectedOperationType, bool is_write>
    struct registered_io_buffer_with_connected_operation
    {
        // read buffer
        alignas(DMA_PAGE_SIZE)
            std::byte buffer[is_write ? WRITE_BUFFER_SIZE : READ_BUFFER_SIZE];
        ConnectedOperationType state[0];

        constexpr registered_io_buffer_with_connected_operation() {}
    };
    friend struct
        registered_io_buffer_with_connected_operation_unique_ptr_deleter;
    struct registered_io_buffer_with_connected_operation_unique_ptr_deleter
    {
        void operator()(erased_connected_operation *p) const
        {
            const bool is_write = p->is_write();
            auto *buffer = (unsigned char *)p -
                           (is_write ? WRITE_BUFFER_SIZE : READ_BUFFER_SIZE);
            assert(((uintptr_t)buffer & (CPU_PAGE_SIZE - 1)) == 0);
            auto *io = p->executor();
            p->~erased_connected_operation();
#ifndef NDEBUG
            memset((void *)p, 0xff, MAX_CONNECTED_OPERATION_SIZE);
            memset((void *)buffer, 0xff, READ_BUFFER_SIZE);
#endif
            if (is_write) {
                io->wr_pool_.release(buffer);
            }
            else {
                io->rd_pool_.release(buffer);
            }
        }
    };
    using erased_connected_operation_unique_ptr_type = std::unique_ptr<
        erased_connected_operation,
        registered_io_buffer_with_connected_operation_unique_ptr_deleter>;
    template <sender Sender, receiver Receiver>
    using connected_operation_unique_ptr_type = std::unique_ptr<
        decltype(connect(
            std::declval<AsyncIO &>(), std::declval<Sender>(),
            std::declval<Receiver>())),
        registered_io_buffer_with_connected_operation_unique_ptr_deleter>;

private:
    template <bool is_write, class buffer_value_type, class F>
    auto _make_connected_impl(F &&connect)
    {
        using connected_type = decltype(connect());
        static_assert(sizeof(connected_type) <= MAX_CONNECTED_OPERATION_SIZE);
        auto *mem = (is_write ? wr_pool_ : rd_pool_).alloc();
        MONAD_ASSERT(mem != nullptr);
        assert(((uintptr_t)mem & (CPU_PAGE_SIZE - 1)) == 0);
        auto read_size =
            is_write ? rwbuf_.get_write_size() : rwbuf_.get_read_size();
        (void)read_size;
        assert(
            read_size >= (is_write ? WRITE_BUFFER_SIZE : READ_BUFFER_SIZE) +
                             sizeof(connected_type));
        assert(((void)mem[0], true));
        auto *buffer = new (mem) registered_io_buffer_with_connected_operation<
            connected_type,
            is_write>;
        auto ret = std::unique_ptr<
            connected_type,
            registered_io_buffer_with_connected_operation_unique_ptr_deleter>(
            new (buffer->state) connected_type(connect()));
        ret->sender().reset(
            ret->sender().offset(),
            {(buffer_value_type *)buffer, ret->sender().buffer().size()});
        return ret;
    }

public:
    //! Construct into a registered i/o buffer a connected state for an i/o read
    //! or write (not timed delay)
    template <sender Sender, receiver Receiver>
        requires(requires(
            Receiver r, erased_connected_operation *o,
            typename Sender::result_type x) { r.set_value(o, std::move(x)); })
    auto make_connected(Sender &&sender, Receiver &&receiver)
    {
        using buffer_value_type = typename Sender::buffer_type::element_type;
        constexpr bool is_write = std::is_const_v<buffer_value_type>;
        return _make_connected_impl<is_write, buffer_value_type>([&] {
            return connect(*this, std::move(sender), std::move(receiver));
        });
    }
    //! Construct into a registered i/o buffer a connected state for an i/o read
    //! or write (not timed delay)
    template <
        sender Sender, receiver Receiver, class... SenderArgs,
        class... ReceiverArgs>
        requires(
            requires(
                Receiver r, erased_connected_operation *o,
                typename Sender::result_type x) {
                r.set_value(o, std::move(x));
            } &&
            std::is_constructible_v<Sender, SenderArgs...> &&
            std::is_constructible_v<Receiver, ReceiverArgs...>)
    auto make_connected(
        std::piecewise_construct_t _, std::tuple<SenderArgs...> &&sender_args,
        std::tuple<ReceiverArgs...> &&receiver_args)
    {
        using buffer_value_type = typename Sender::buffer_type::element_type;
        constexpr bool is_write = std::is_const_v<buffer_value_type>;
        return _make_connected_impl<is_write, buffer_value_type>([&] {
            return connect<Sender, Receiver>(
                *this, _, std::move(sender_args), std::move(receiver_args));
        });
    }
};
using erased_connected_operation_ptr =
    AsyncIO::erased_connected_operation_unique_ptr_type;

static_assert(sizeof(AsyncIO) == 72);
static_assert(alignof(AsyncIO) == 8);

/*! \struct erased_connected_operation_deleter_io_receiver
\brief A receiver which deallocates the `AsyncIO::connected_operation_type` i.e.
returns the read or write registered buffer to the pool for later reuse.
*/
struct erased_connected_operation_deleter_io_receiver
{
    template <class T>
    void set_value(erased_connected_operation *rawstate, result<T> res)
    {
        MONAD_ASSERT(res);
        AsyncIO::erased_connected_operation_unique_ptr_type{rawstate};
    }
    void reset() {}
};

MONAD_ASYNC_NAMESPACE_END
