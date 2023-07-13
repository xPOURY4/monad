#pragma once

#include <monad/trie/util.hpp>

#include <monad/io/buffer_pool.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <monad/mem/allocators.hpp>
#include <monad/trie/node_helper.hpp>

#include <fcntl.h>

#include <boost/outcome/experimental/status_result.hpp>
#include <boost/outcome/try.hpp>

#include <cassert>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <tuple>

MONAD_TRIE_NAMESPACE_BEGIN

template <class T>
using result = ::boost::outcome_v2::experimental::status_result<T>;
using ::boost::outcome_v2::experimental::failure;
using ::boost::outcome_v2::experimental::posix_code;
using ::boost::outcome_v2::experimental::success;

class AsyncIO;
class read_single_buffer_sender;

enum class uring_data_type_t : unsigned char
{
    UNKNOWN = 0,
    IS_APPEND,
    IS_READ
};

/* The following Sender-Receiver implementation is loosely based on
https://wg21.link/p2300 `std::execution`. We don't actually
implement P2300 because:

1. It is hard on compile times for the benefits it gives.

2. It is heavy on the templates for the benefits it gives.

3. It suffers from doing too much, but also too little, unsurprising
given its painful gestation through the standards committee where
arguably the committee eventually settled a "best we can do considering"
design.

4. We don't want in any way for our implementation of Senders-Receivers
to ever collide with the standard one, so we are intentionally very
incompatible.



All that said, the Senders-Receivers abstraction is the correct one,
so we employ it here, and if you in the future need to use this code,
it is semantically similar to P2300. To use:

1. Create the Sender for the operation you wish to perform, configured
with the arguments you wish.

2. Create the Receiver for how you would like the operation completion
to be implemented.

3. Connect your Sender and your Receiver into a connected operation
state. This moves your Sender and your Receiver into the operation
state.

4. Submit the connected operation state to AsyncIO, which is taken
by reference. You cannot touch this object in any way after this.
Note that connection operation states cannot be moved nor copied.

5. When the operation completes, its Receiver shall be invoked.

6. You are now allowed to touch the connection operation state. For
most cases, destroying it is the easiest.



If you really care about performance, there is a more awkward to
use option:

1. In your currently not-in-use connected operation state, set
the Sender and Receiver to what you need them to be.

2. Submit the connected operation state to AsyncIO, which is taken
by reference. You cannot touch this object in any way after this.
Note that connection operation states cannot be moved nor copied.

3. When the operation completes, its Receiver shall be invoked.

4. You are now allowed to touch the connection operation state.
You should call `reset()` on it to free any internal resources,
which will also call `reset()` on its sender and receiver.
*/

class erased_connected_operation
{
protected:
    const uring_data_type_t
        _rw_flag; // TEMPORARY: for layout compatibility with write_uring_data_t
    bool _being_executed{false};
    AsyncIO &_io;

    constexpr erased_connected_operation(bool is_append, AsyncIO &io)
        : _rw_flag(
              is_append ? uring_data_type_t::IS_APPEND
                        : uring_data_type_t::IS_READ)
        , _io(io)
    {
    }

public:
    virtual ~erased_connected_operation()
    {
        MONAD_ASSERT(!_being_executed);
    }
    bool is_read() const noexcept
    {
        return _rw_flag == uring_data_type_t::IS_READ;
    }
    bool is_append() const noexcept
    {
        return _rw_flag == uring_data_type_t::IS_APPEND;
    }
    bool is_currently_being_executed() const noexcept
    {
        return _being_executed;
    }
    AsyncIO &executor() noexcept
    {
        return _io;
    }
    virtual void completed(result<size_t> bytes_transferred) = 0;
    void reset() {}
};

template <class T>
concept sender =
    std::is_move_constructible_v<T> && std::is_destructible_v<T> &&
    std::is_invocable_r_v<result<void>, T, erased_connected_operation *> &&
    requires { typename T::result_type; };

template <class T>
concept receiver = std::is_move_constructible_v<T> && std::is_destructible_v<T>;

/*! \class connected_operation
\brief A connected sender-receiver pair which implements operation state.

The customisation point is the free function `connect()` which may
be overloaded to return an extended `connected_operation` type containing
additional i/o specific state.

`connected_operation` cannot be relocated in memory, and must not be
destructed between submission and completion.

`connected_operation` can be reused if its sender-receiver pair supports
that.
*/
template <sender Sender, receiver Receiver>
class connected_operation final : public erased_connected_operation
{
public:
    using sender_type = Sender;
    using receiver_type = Receiver;
    //! True if this connected operation state is resettable and reusable
    static constexpr bool is_resettable = requires {
        &sender_type::reset;
        &receiver_type::reset;
    };

private:
    Sender _sender;
    Receiver _receiver;

    // This will devirtualise and usually disappear entirely from codegen
    virtual void completed(result<size_t> bytes_transferred) override
    {
        this->_being_executed = false;
        if constexpr (requires(Sender x) {
                          x.completed(
                              this,
                              static_cast<result<size_t> &&>(
                                  bytes_transferred));
                      }) {
            _receiver.set_value(
                this,
                _sender.completed(
                    this, static_cast<result<size_t> &&>(bytes_transferred)));
        }
        else {
            _receiver.set_value(
                this, static_cast<result<size_t> &&>(bytes_transferred));
        }
    }

public:
    connected_operation(
        AsyncIO &io, sender_type &&sender, receiver_type &&receiver)
        : erased_connected_operation(false, io)
        , _sender(static_cast<Sender &&>(sender))
        , _receiver(static_cast<Receiver &&>(receiver))
    {
    }
    template <class... SenderArgs, class... ReceiverArgs>
    connected_operation(
        AsyncIO &io, std::piecewise_construct_t,
        std::tuple<SenderArgs...> sender_args,
        std::tuple<ReceiverArgs...> receiver_args)
        : erased_connected_operation(false, io)
        , _sender(std::make_from_tuple<Sender>(std::move(sender_args)))
        , _receiver(std::make_from_tuple<Receiver>(std::move(receiver_args)))
    {
    }

    connected_operation(const connected_operation &) = delete;
    connected_operation(connected_operation &&) = delete;
    connected_operation &operator=(const connected_operation &) = delete;
    connected_operation &operator=(connected_operation &&) = delete;
    ~connected_operation() = default;

    sender_type &sender() & noexcept
    {
        return _sender;
    }
    sender_type sender() && noexcept
    {
        return static_cast<sender_type &&>(_sender);
    }
    receiver_type &receiver() & noexcept
    {
        return _receiver;
    }
    receiver_type receiver() && noexcept
    {
        return static_cast<receiver_type &&>(_receiver);
    }

    //! Initiates the operation. If successful do NOT modify anything after this
    //! until after completion, it may cause a silent page copy-on-write.
    result<void> initiate() noexcept
    {
        this->_being_executed = true;
        // Prevent compiler reordering write of _being_executed after this point
        // without using actual atomics.
        std::atomic_signal_fence(std::memory_order_release);
        auto r = _sender(this);
        if (!r) {
            this->_being_executed = false;
        }
        return r;
    }

    //! Resets the operation state. Only available if both sender and receiver
    //! implement `reset()`
    template <class... SenderArgs, class... ReceiverArgs>
        requires(is_resettable)
    void reset(
        std::tuple<SenderArgs...> sender_args,
        std::tuple<ReceiverArgs...> receiver_args)
    {
        MONAD_ASSERT(!this->_being_executed);
        erased_connected_operation::reset();
        std::apply(
            [this](auto &&...args) { _sender.reset(std::move(args)...); },
            std::move(sender_args));
        std::apply(
            [this](auto &&...args) { _receiver.reset(std::move(args)...); },
            std::move(receiver_args));
    }
};
//! Default connect customisation point taking sender and receiver by value,
//! requires receiver to be compatible with sender.
template <sender Sender, receiver Receiver>
    requires(requires(
        Receiver r, erased_connected_operation *o,
        typename Sender::result_type x) { r.set_value(o, std::move(x)); })
inline connected_operation<Sender, Receiver>
connect(AsyncIO &io, Sender &&sender, Receiver &&receiver)
{
    return connected_operation<Sender, Receiver>(
        io, static_cast<Sender &&>(sender), static_cast<Receiver &&>(receiver));
}
//! Alternative connect customisation point taking piecewise construction args,
//! requires receiver to be compatible with sender
template <
    sender Sender, receiver Receiver, class... SenderArgs,
    class... ReceiverArgs>
    requires(
        requires(
            Receiver r, erased_connected_operation *o,
            typename Sender::result_type x) { r.set_value(o, std::move(x)); } &&
        std::is_constructible_v<Sender, SenderArgs...> &&
        std::is_constructible_v<Receiver, ReceiverArgs...>)
inline connected_operation<Sender, Receiver> connect(
    AsyncIO &io, std::piecewise_construct_t _,
    std::tuple<SenderArgs...> &&sender_args,
    std::tuple<ReceiverArgs...> &&receiver_args)
{
    return connected_operation<Sender, Receiver>(
        io, _, std::move(sender_args), std::move(receiver_args));
}

// helper struct that records IO stats
struct IORecord
{
    unsigned inflight{0};
    unsigned inflight_rd{0};
    unsigned nreads{0};
};

class AsyncIO final
{
    friend class read_single_buffer_sender;
    constexpr static unsigned READ = 0, WRITE = 1;

    // TODO: using user_data_t = variant<update_data_t, write_data_t>
    struct write_uring_data_t final
        :
        /* TEMPORARY, for layout compatibility with sender-receiver */
        public erased_connected_operation
    {
        unsigned char *buffer;

        write_uring_data_t(AsyncIO &io, unsigned char *b)
            : erased_connected_operation(true, io)
            , buffer(b)
        {
        }

        virtual void completed(result<size_t>) override final
        {
            abort();
        }

        using allocator_type =
            allocators::boost_unordered_pool_allocator<write_uring_data_t>;
        static allocator_type &pool()
        {
            static allocator_type v;
            return v;
        }
        using unique_ptr_type = std::unique_ptr<
            write_uring_data_t, allocators::unique_ptr_allocator_deleter<
                                    allocator_type, &write_uring_data_t::pool>>;
        static unique_ptr_type make(AsyncIO &io, unsigned char *b)
        {
            return allocators::
                allocate_unique<allocator_type, &write_uring_data_t::pool>(
                    io, b);
        }
    };

    static_assert(sizeof(write_uring_data_t) == 32);
    static_assert(alignof(write_uring_data_t) == 8);

    int fds_[2];
    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;

    unsigned char *write_buffer_;
    size_t buffer_idx_;
    file_offset_t block_off_;

    // IO records
    IORecord records_;

    void submit_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data,
        bool is_write);

    void submit_write_request(
        unsigned char *buffer, file_offset_t const offset, unsigned write_size);

    void poll_uring();

public:
    AsyncIO(
        std::pair<int, int> fds, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf, file_offset_t block_off)
        : fds_{fds.first, fds.second}
        , uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
        , write_buffer_(wr_pool_.alloc())
        , buffer_idx_(0)
        , block_off_(round_up_align<DISK_PAGE_BITS>(block_off))
    {
        MONAD_ASSERT(write_buffer_);
        MONAD_ASSERT(fds_[WRITE] != -1);
        MONAD_ASSERT(fds_[READ] != -1);

        // register files
        MONAD_ASSERT(!io_uring_register_files(
            const_cast<io_uring *>(&uring_.get_ring()), fds_, 2));
    }
    AsyncIO(
        std::filesystem::path &p, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf, file_offset_t block_off)
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
              ring, rwbuf, block_off)
    {
    }
    AsyncIO(
        use_anonymous_inode_tag, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf, file_offset_t block_off)
        : AsyncIO(
              []() -> std::pair<int, int> {
                  int fds[2];
                  fds[0] = make_temporary_inode();
                  fds[1] = dup(fds[0]);
                  return {fds[0], fds[1]};
              }(),
              ring, rwbuf, block_off)
    {
    }

    ~AsyncIO()
    {
        wait_until_done();
        wr_pool_.release(write_buffer_);
        MONAD_ASSERT(!records_.inflight);

        MONAD_ASSERT(!io_uring_unregister_files(
            const_cast<io_uring *>(&uring_.get_ring())));

        ::close(fds_[READ]);
        ::close(fds_[WRITE]);
    }

    size_t poll(size_t count = size_t(-1))
    {
        // handle the last buffer to write
        if (buffer_idx_ > 1) {
            submit_write_request(
                write_buffer_, block_off_, rwbuf_.get_write_size());
        }
        size_t n = 0;
        for (; n < count && records_.inflight > 0; n++) {
            poll_uring();
        }
        return n;
    }

    void wait_until_done()
    {
        poll(size_t(-1));
    }

    struct async_write_node_result
    {
        file_offset_t offset_written_to;
        unsigned bytes_appended;
    };
    async_write_node_result async_write_node(merkle_node_t const *const node);

    void flush_last_buffer()
    {
        // Write the last pending buffer for current block.
        // mainly useful for unit test purposes for now, where updates are not
        // enough to fill single buffer. So there's gap where node is
        // deallocated but not yet reaching disk for read.
        // Always cache the latest version(s) in memory will resolve this
        // problem nicely.
        if (buffer_idx_ > 1) {
            unsigned write_size = round_up_align<DISK_PAGE_BITS>(buffer_idx_);
            submit_write_request(write_buffer_, block_off_, write_size);
            write_buffer_ = wr_pool_.alloc();
            MONAD_ASSERT(write_buffer_);
            block_off_ += write_size;
            buffer_idx_ = 0;

            poll_uring();
            MONAD_ASSERT(records_.inflight == 0);
        }
    }
    // invoke at the end of each block
    async_write_node_result flush(merkle_node_t *root)
    {
        while (records_.inflight) {
            poll_uring();
        }
        // only write root to disk if trie is not empty
        // root write is pending, will submit or poll in next round
        auto root_off = root->valid_mask
                            ? async_write_node(root)
                            : async_write_node_result{INVALID_OFFSET, 0};

        MONAD_ASSERT(records_.inflight <= 1);

        records_.nreads = 0;
        return root_off;
    }

    void submit_read_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap some
        while (records_.inflight >= uring_.get_sq_entries()) {
            poll_uring();
        }
        submit_request(buffer, offset, uring_data, false);
        ++records_.inflight;
        ++records_.inflight_rd;
        ++records_.nreads;
    }

    constexpr int get_rd_fd() noexcept
    {
        return fds_[READ];
    }

    /* This isn't the ideal place to put this, but only AsyncIO knows how to
    get i/o buffers into which to place connected i/o states.
    */
    static constexpr size_t BUFFER_SIZE = round_up_align<DISK_PAGE_BITS>(
        uint16_t(MAX_DISK_NODE_SIZE + 2 * DISK_PAGE_SIZE - 1));
    template <class ConnectedOperationType>
    struct registered_io_buffer_with_connected_operation
    {
        // read buffer
        alignas(DMA_PAGE_SIZE) std::byte buffer[BUFFER_SIZE];
        ConnectedOperationType state[0];

        constexpr registered_io_buffer_with_connected_operation() {}
    };
    friend struct
        registered_io_buffer_with_connected_operation_unique_ptr_deleter;
    struct registered_io_buffer_with_connected_operation_unique_ptr_deleter
    {
        void operator()(erased_connected_operation *p) const
        {
            auto *buffer = (unsigned char *)p - BUFFER_SIZE;
            assert(((uintptr_t)buffer & (CPU_PAGE_SIZE - 1)) == 0);
            auto &io = p->executor();
            p->~erased_connected_operation();
            io.rd_pool_.release(buffer);
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

    template <sender Sender, receiver Receiver>
        requires(requires(
            Receiver r, erased_connected_operation *o,
            typename Sender::result_type x) { r.set_value(o, std::move(x)); })
    auto make_connected(Sender &&sender, Receiver &&receiver)
    {
        using connected_type =
            decltype(connect(*this, std::move(sender), std::move(receiver)));
        static_assert(sizeof(connected_type) <= CPU_PAGE_SIZE);
        auto *mem = rd_pool_.alloc();
        MONAD_ASSERT(mem != nullptr);
        assert(((uintptr_t)mem & (CPU_PAGE_SIZE - 1)) == 0);
        assert(rwbuf_.get_read_size() >= BUFFER_SIZE + sizeof(connected_type));
        assert(((void)mem[0], true));
        auto *buffer = new (mem)
            registered_io_buffer_with_connected_operation<connected_type>;
        auto ret = std::unique_ptr<
            connected_type,
            registered_io_buffer_with_connected_operation_unique_ptr_deleter>(
            new (buffer->state) connected_type(
                connect(*this, std::move(sender), std::move(receiver))));
        ret->sender().reset(
            ret->sender().offset(),
            {(std::byte *)buffer, ret->sender().buffer().size()});
        return ret;
    }
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
        using connected_type = decltype(connect<Sender, Receiver>(
            *this, _, std::move(sender_args), std::move(receiver_args)));
        static_assert(sizeof(connected_type) <= CPU_PAGE_SIZE);
        auto *mem = rd_pool_.alloc();
        MONAD_ASSERT(mem != nullptr);
        assert(((uintptr_t)mem & (CPU_PAGE_SIZE - 1)) == 0);
        assert(rwbuf_.get_read_size() >= BUFFER_SIZE + sizeof(connected_type));
        assert(((void)mem[0], true));
        auto *buffer = new (mem)
            registered_io_buffer_with_connected_operation<connected_type>;
        auto ret = std::unique_ptr<
            connected_type,
            registered_io_buffer_with_connected_operation_unique_ptr_deleter>(
            new (buffer->state) connected_type(connect<Sender, Receiver>(
                *this, _, std::move(sender_args), std::move(receiver_args))));
        ret->sender().change_io_buffer_address(
            {(std::byte *)buffer, BUFFER_SIZE});
        return ret;
    }
};
using erased_connected_operation_ptr =
    AsyncIO::erased_connected_operation_unique_ptr_type;

static_assert(sizeof(AsyncIO) == 80);
static_assert(alignof(AsyncIO) == 8);

MONAD_TRIE_NAMESPACE_END
