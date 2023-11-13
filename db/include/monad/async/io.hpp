#pragma once

#include <monad/async/connected_operation.hpp>

#include <monad/async/storage_pool.hpp>

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
#include <iostream>
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
    using _storage_pool = class storage_pool;
    using cnv_chunk = _storage_pool::cnv_chunk;
    using seq_chunk = _storage_pool::seq_chunk;

    template <class T>
    struct chunk_ptr_
    {
        std::shared_ptr<T> ptr;
        int io_uring_read_fd{-1}, io_uring_write_fd{-1}; // NOT POSIX fds!

        constexpr chunk_ptr_() = default;
        constexpr chunk_ptr_(std::shared_ptr<T> ptr_)
            : ptr(std::move(ptr_))
            , io_uring_read_fd(ptr ? ptr->read_fd().first : -1)
            , io_uring_write_fd(ptr ? ptr->write_fd(0).first : -1)
        {
        }
    };

    pid_t const owning_tid_;
    class storage_pool *storage_pool_{nullptr};
    chunk_ptr_<cnv_chunk> cnv_chunk_;
    std::vector<chunk_ptr_<seq_chunk>> seq_chunks_;
    struct
    {
        int msgread, msgwrite;
    } fds_;
    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;

    // IO records
    IORecord records_;

    AsyncIO(monad::io::Ring &ring, monad::io::Buffers &rwbuf);
    void init_(std::span<int> fds);

    void submit_request_(
        std::span<std::byte> buffer, chunk_offset_t chunk_and_offset,
        void *uring_data);
    void submit_request_(
        std::span<std::byte const> buffer, chunk_offset_t chunk_and_offset,
        void *uring_data);
    void submit_request_(timed_invocation_state *state, void *uring_data);

    void poll_uring_while_submission_queue_full_();
    bool poll_uring_(bool blocking);

public:
    AsyncIO(
        class storage_pool &pool, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf);
    ~AsyncIO();

    pid_t owning_thread_id() const noexcept
    {
        return owning_tid_;
    }

    class storage_pool &storage_pool() noexcept
    {
        assert(storage_pool_ != nullptr);
        return *storage_pool_;
    }
    const class storage_pool &storage_pool() const noexcept
    {
        assert(storage_pool_ != nullptr);
        return *storage_pool_;
    }

    size_t chunk_count() const noexcept
    {
        return seq_chunks_.size();
    }
    file_offset_t chunk_capacity(size_t id) const noexcept
    {
        MONAD_ASSERT(id < seq_chunks_.size());
        return seq_chunks_[id].ptr->capacity();
    }

    //! The instance for this thread
    static AsyncIO *thread_instance() noexcept
    {
        return detail::AsyncIO_thread_instance();
    }

    unsigned io_in_flight() const noexcept
    {
        return records_.inflight_rd + records_.inflight_wr +
               records_.inflight_tm +
               records_.inflight_ts.load(std::memory_order_relaxed) +
               deferred_initiations_in_flight();
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

    unsigned deferred_initiations_in_flight() const noexcept;

    unsigned threadsafeops_in_flight() const noexcept
    {
        return records_.inflight_ts.load(std::memory_order_relaxed);
    }

    // Useful for taking a copy of anonymous inode files used by the unit tests
    void dump_fd_to(size_t which, std::filesystem::path const &path);

    // Blocks until at least one completion is processed, returning number
    // of completions processed.
    size_t poll_blocking(size_t count = 1)
    {
        size_t n = 0;
        for (; n < count; n++) {
            if (!poll_uring_(n == 0)) {
                break;
            }
        }
        return n;
    }
    std::optional<size_t>
    poll_blocking_if_not_within_completions(size_t count = 1)
    {
        if (detail::AsyncIO_per_thread_state().am_within_completions()) {
            return std::nullopt;
        }
        return poll_blocking(count);
    }

    // Never blocks
    size_t poll_nonblocking(size_t count = size_t(-1))
    {
        size_t n = 0;
        for (; n < count; n++) {
            if (!poll_uring_(false)) {
                break;
            }
        }
        return n;
    }
    std::optional<size_t>
    poll_nonblocking_if_not_within_completions(size_t count = size_t(-1))
    {
        if (detail::AsyncIO_per_thread_state().am_within_completions()) {
            return std::nullopt;
        }
        return poll_nonblocking(count);
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
        std::cout << "nreads: " << records_.nreads << std::endl;
        records_.nreads = 0;
    }

    bool submit_read_request(
        std::span<std::byte> buffer, chunk_offset_t offset,
        erased_connected_operation *uring_data)
    {
        submit_request_(buffer, offset, uring_data);
        ++records_.inflight_rd;
        ++records_.nreads;
        return false;
    }

    void submit_write_request(
        std::span<std::byte const> buffer, chunk_offset_t offset,
        erased_connected_operation *uring_data)
    {
        submit_request_(buffer, offset, uring_data);
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
        timed_invocation_state *info, erased_connected_operation *uring_data)
    {
        submit_request_(info, uring_data);
        ++records_.inflight_tm;
    }

    void submit_threadsafe_invocation_request(
        erased_connected_operation *uring_data);

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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
        ConnectedOperationType state[0];
#pragma GCC diagnostic pop
        constexpr registered_io_buffer_with_connected_operation() {}
    };
    friend struct
        registered_io_buffer_with_connected_operation_unique_ptr_deleter;
    struct registered_io_buffer_with_connected_operation_unique_ptr_deleter
    {
        void operator()(erased_connected_operation *p) const
        {
            bool const is_write = p->is_write();
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
    auto make_connected_impl_(F &&connect)
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
        assert(
            ret->sender().buffer().data() ==
            nullptr); // Did you accidentally pass in a foreign buffer to use?
                      // Can't do that, must use buffer returned.
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
        return make_connected_impl_<is_write, buffer_value_type>([&] {
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
        return make_connected_impl_<is_write, buffer_value_type>([&] {
            return connect<Sender, Receiver>(
                *this, _, std::move(sender_args), std::move(receiver_args));
        });
    }

    template <class Base, sender Sender, receiver Receiver>
    void notify_operation_initiation_success_(
        detail::connected_operation_storage<Base, Sender, Receiver> *state)
    {
        if constexpr (detail::connected_operation_storage<
                          Base,
                          Sender,
                          Receiver>::is_write()) {
            auto *p =
                erased_connected_operation::rbtree_node_traits::to_node_ptr(
                    state);
            erased_connected_operation::rbtree_node_traits::set_key(
                p, state->sender().offset().raw());
            assert(p->key == state->sender().offset().raw());
            extant_write_operations_::init(p);
            auto pred = [](auto const *a, auto const *b) {
                auto get_key = [](const auto *a) {
                    return erased_connected_operation::rbtree_node_traits::
                        get_key(a);
                };
                return get_key(a) > get_key(b);
            };
            extant_write_operations_::insert_equal_lower_bound(
                &extant_write_operations_header_, p, pred);
        }
    }
    template <class Base, sender Sender, receiver Receiver>
    void notify_operation_reset_(
        detail::connected_operation_storage<Base, Sender, Receiver> *state)
    {
        (void)state;
    }
    template <class Base, sender Sender, receiver Receiver, class T>
    void notify_operation_completed_(
        detail::connected_operation_storage<Base, Sender, Receiver> *state,
        result<T> &res)
    {
        if constexpr (detail::connected_operation_storage<
                          Base,
                          Sender,
                          Receiver>::is_write()) {
            extant_write_operations_::erase(
                &extant_write_operations_header_,
                erased_connected_operation::rbtree_node_traits::to_node_ptr(
                    state));
        }
        else if constexpr (
            detail::connected_operation_storage<Base, Sender, Receiver>::
                is_read() &&
            !std::is_void_v<T>) {
            if (res && res.assume_value() > 0) {
                // If we filled the data from extant write buffers above, adjust
                // bytes transferred to account for that.
                res = success(
                    res.assume_value() +
                    erased_connected_operation::rbtree_node_traits::get_key(
                        state));
            }
        }
    }

private:
    using extant_write_operations_ = ::boost::intrusive::rbtree_algorithms<
        erased_connected_operation::rbtree_node_traits>;
    erased_connected_operation::rbtree_node_traits::node
        extant_write_operations_header_;
};
using erased_connected_operation_ptr =
    AsyncIO::erased_connected_operation_unique_ptr_type;

static_assert(sizeof(AsyncIO) == 160);
static_assert(alignof(AsyncIO) == 8);

MONAD_ASYNC_NAMESPACE_END
