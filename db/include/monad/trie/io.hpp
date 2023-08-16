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
#include <concepts>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <tuple>

MONAD_TRIE_NAMESPACE_BEGIN

template <class T>
using result = ::boost::outcome_v2::experimental::status_result<T>;
using ::boost::outcome_v2::experimental::errc;
using ::boost::outcome_v2::experimental::failure;
using ::boost::outcome_v2::experimental::posix_code;
using ::boost::outcome_v2::experimental::success;

class AsyncIO;
class read_single_buffer_sender;
class erased_connected_operation;

template <class T>
concept sender =
    std::is_destructible_v<T> &&
    std::is_invocable_r_v<result<void>, T, erased_connected_operation *> &&
    requires { typename T::result_type; } &&
    (
        std::is_same_v<typename T::result_type, result<void>> ||
        requires(T s, erased_connected_operation *o, result<void> x) {
            {
                s.completed(o, std::move(x))
            } -> std::same_as<typename T::result_type>;
        } || std::is_same_v<typename T::result_type, result<size_t>> ||
        requires(T s, erased_connected_operation *o, result<size_t> x) {
            {
                s.completed(o, std::move(x))
            } -> std::same_as<typename T::result_type>;
        });

template <class T>
concept receiver = std::is_destructible_v<T>;

template <class Sender, class Receiver>
concept compatible_sender_receiver =
    sender<Sender> && receiver<Receiver> &&
    requires(
        Receiver r, erased_connected_operation *o,
        typename Sender::result_type x) { r.set_value(o, std::move(x)); };

template <sender Sender, receiver Receiver>
class connected_operation;

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
    template <sender Sender, receiver Receiver>
    friend class connected_operation;

protected:
    enum class _operation_type_t : uint8_t
    {
        unknown,
        read,
        write
    } _operation_type{_operation_type_t::unknown};
    bool _being_executed{false};
    AsyncIO *_io{nullptr};
    erased_connected_operation *_next{nullptr};

    constexpr erased_connected_operation() {}

    constexpr erased_connected_operation(
        _operation_type_t operation_type, AsyncIO &io)
        : _operation_type(operation_type)
        , _io(&io)
    {
    }

public:
    virtual ~erased_connected_operation()
    {
        MONAD_ASSERT(!_being_executed);
    }
    bool is_unknown_operation_type() const noexcept
    {
        return _operation_type == _operation_type_t::unknown;
    }
    bool is_read() const noexcept
    {
        return _operation_type == _operation_type_t::read;
    }
    bool is_write() const noexcept
    {
        return _operation_type == _operation_type_t::write;
    }
    bool is_currently_being_executed() const noexcept
    {
        return _being_executed;
    }
    AsyncIO &executor() noexcept
    {
        return *_io;
    }
    erased_connected_operation *&next() noexcept
    {
        return _next;
    }
    virtual void completed(result<void> res) = 0;
    virtual void completed(result<size_t> bytes_transferred) = 0;
    // Overload ambiguity resolver
    void completed(BOOST_OUTCOME_V2_NAMESPACE::success_type<void> _)
    {
        completed(result<void>(_));
    }
    void reset() {}
};

namespace detail
{
    template <class Base, sender Sender, receiver Receiver>
    struct connected_operation_storage : public Base
    {
        friend class AsyncIO;

        virtual void completed(result<void>) override
        {
            // If you reach here, somebody called a void completed()
            // on a bytes transferred type connected operation
            abort();
        }
        virtual void completed(result<size_t> res) override
        {
            // Decay to the void type
            completed(result<void>(std::move(res).as_failure()));
        }

    public:
        using sender_type = Sender;
        using receiver_type = Receiver;
        //! True if this connected operation state is resettable and reusable
        static constexpr bool is_resettable = requires {
            &sender_type::reset;
            &receiver_type::reset;
        };

    protected:
        Sender _sender;
        Receiver _receiver;

        // Deduce what kind of connected operation we are
        static constexpr erased_connected_operation::_operation_type_t
            _operation_type = []() constexpr {
                if constexpr (requires {
                                  typename Sender::buffer_type::element_type;
                              }) {
                    constexpr bool is_const = std::is_const_v<
                        typename Sender::buffer_type::element_type>;
                    return is_const ? erased_connected_operation::
                                          _operation_type_t::write
                                    : erased_connected_operation::
                                          _operation_type_t::read;
                }
                else {
                    return erased_connected_operation::_operation_type_t::
                        unknown;
                }
            }();

    public:
        connected_operation_storage(
            sender_type &&sender, receiver_type &&receiver)
            : _sender(static_cast<Sender &&>(sender))
            , _receiver(static_cast<Receiver &&>(receiver))
        {
        }
        connected_operation_storage(
            AsyncIO &io, sender_type &&sender, receiver_type &&receiver)
            : erased_connected_operation(_operation_type, io)
            , _sender(static_cast<Sender &&>(sender))
            , _receiver(static_cast<Receiver &&>(receiver))
        {
        }
        template <class... SenderArgs, class... ReceiverArgs>
        connected_operation_storage(
            std::piecewise_construct_t, std::tuple<SenderArgs...> sender_args,
            std::tuple<ReceiverArgs...> receiver_args)
            : _sender(std::make_from_tuple<Sender>(std::move(sender_args)))
            , _receiver(
                  std::make_from_tuple<Receiver>(std::move(receiver_args)))
        {
        }
        template <class... SenderArgs, class... ReceiverArgs>
        connected_operation_storage(
            AsyncIO &io, std::piecewise_construct_t,
            std::tuple<SenderArgs...> sender_args,
            std::tuple<ReceiverArgs...> receiver_args)
            : erased_connected_operation(_operation_type, io)
            , _sender(std::make_from_tuple<Sender>(std::move(sender_args)))
            , _receiver(
                  std::make_from_tuple<Receiver>(std::move(receiver_args)))
        {
        }

        connected_operation_storage(const connected_operation_storage &) =
            delete;
        connected_operation_storage(connected_operation_storage &&) = delete;
        connected_operation_storage &
        operator=(const connected_operation_storage &) = delete;
        connected_operation_storage &
        operator=(connected_operation_storage &&) = delete;
        ~connected_operation_storage() = default;

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

        static constexpr bool is_unknown_operation_type() noexcept
        {
            return _operation_type ==
                   erased_connected_operation::_operation_type_t::unknown;
        }
        static constexpr bool is_read() noexcept
        {
            return _operation_type ==
                   erased_connected_operation::_operation_type_t::read;
        }
        static constexpr bool is_write() noexcept
        {
            return _operation_type ==
                   erased_connected_operation::_operation_type_t::write;
        }

        //! Initiates the operation. If successful do NOT modify anything after
        //! this until after completion, it may cause a silent page
        //! copy-on-write.
        result<void> initiate() noexcept
        {
            this->_being_executed = true;
            // Prevent compiler reordering write of _being_executed after this
            // point without using actual atomics.
            std::atomic_signal_fence(std::memory_order_release);
            auto r = _sender(this);
            if (!r) {
                this->_being_executed = false;
            }
            return r;
        }

        //! Resets the operation state. Only available if both sender and
        //! receiver implement `reset()`
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

    template <
        class Base, sender Sender, receiver Receiver,
        bool enable =
            requires(
                Receiver r, erased_connected_operation *o, result<void> res) {
                r.set_value(o, std::move(res));
            } ||
            requires(
                Sender s, Receiver r, erased_connected_operation *o,
                result<void> res) {
                r.set_value(o, s.completed(o, std::move(res)));
            }>
    struct connected_operation_void_completed_implementation : public Base
    {
        using Base::Base;
        static constexpr bool _void_completed_enabled = false;
    };
    template <
        class Base, sender Sender, receiver Receiver,
        bool enable =
            requires(
                Receiver r, erased_connected_operation *o, result<size_t> res) {
                r.set_value(o, std::move(res));
            } ||
            requires(
                Sender s, Receiver r, erased_connected_operation *o,
                result<size_t> res) {
                r.set_value(o, s.completed(o, std::move(res)));
            }>
    struct connected_operation_bytes_completed_implementation : public Base
    {
        using Base::Base;
        static constexpr bool _bytes_completed_enabled = false;
    };

    template <class Base, sender Sender, receiver Receiver>
    struct connected_operation_void_completed_implementation<
        Base, Sender, Receiver, true> : public Base
    {
        using Base::Base;
        static constexpr bool _void_completed_enabled = true;

    private:
        // These will devirtualise and usually disappear entirely from codegen
        virtual void completed(result<void> res) override final
        {
            this->_being_executed = false;
            if constexpr (requires(Sender x) {
                              x.completed(
                                  this, static_cast<result<void> &&>(res));
                          }) {
                this->_receiver.set_value(
                    this,
                    this->_sender.completed(
                        this, static_cast<result<void> &&>(res)));
            }
            else {
                this->_receiver.set_value(
                    this, static_cast<result<void> &&>(res));
            }
        }
    };
    template <class Base, sender Sender, receiver Receiver>
    struct connected_operation_bytes_completed_implementation<
        Base, Sender, Receiver, true> : public Base
    {
        using Base::Base;
        static constexpr bool _bytes_completed_enabled = true;

    private:
        // This will devirtualise and usually disappear entirely from codegen
        virtual void completed(result<size_t> bytes_transferred) override final
        {
            this->_being_executed = false;
            if constexpr (requires(Sender x) {
                              x.completed(
                                  this,
                                  static_cast<result<size_t> &&>(
                                      bytes_transferred));
                          }) {
                this->_receiver.set_value(
                    this,
                    this->_sender.completed(
                        this,
                        static_cast<result<size_t> &&>(bytes_transferred)));
            }
            else {
                this->_receiver.set_value(
                    this, static_cast<result<size_t> &&>(bytes_transferred));
            }
        }
    };
}

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
class connected_operation final
    : public detail::connected_operation_void_completed_implementation<
          detail::connected_operation_bytes_completed_implementation<
              detail::connected_operation_storage<
                  erased_connected_operation, Sender, Receiver>,
              Sender, Receiver>,
          Sender, Receiver>
{
    using _base = detail::connected_operation_void_completed_implementation<
        detail::connected_operation_bytes_completed_implementation<
            detail::connected_operation_storage<
                erased_connected_operation, Sender, Receiver>,
            Sender, Receiver>,
        Sender, Receiver>;
    static_assert(
        _base::_void_completed_enabled || _base::_bytes_completed_enabled,
        "If Sender's result_type is neither result<void> nor "
        "result<size_t>, it must provide a completed(result<void>) or "
        "completed(result<size_t>) to transform a completion into the "
        "appropriate result_type value for the Receiver.");

public:
    using _base::_base;
};
//! Default connect customisation point taking sender and receiver by value,
//! requires receiver to be compatible with sender.
template <sender Sender, receiver Receiver>
    requires(compatible_sender_receiver<Sender, Receiver>)
inline connected_operation<Sender, Receiver>
connect(Sender &&sender, Receiver &&receiver)
{
    return connected_operation<Sender, Receiver>(
        static_cast<Sender &&>(sender), static_cast<Receiver &&>(receiver));
}
//! \overload
template <sender Sender, receiver Receiver>
    requires(compatible_sender_receiver<Sender, Receiver>)
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
        compatible_sender_receiver<Sender, Receiver> &&
        std::is_constructible_v<Sender, SenderArgs...> &&
        std::is_constructible_v<Receiver, ReceiverArgs...>)
inline connected_operation<Sender, Receiver> connect(
    std::piecewise_construct_t _, std::tuple<SenderArgs...> &&sender_args,
    std::tuple<ReceiverArgs...> &&receiver_args)
{
    return connected_operation<Sender, Receiver>(
        _, std::move(sender_args), std::move(receiver_args));
}
//! \overload
template <
    sender Sender, receiver Receiver, class... SenderArgs,
    class... ReceiverArgs>
    requires(
        compatible_sender_receiver<Sender, Receiver> &&
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
    int fds_[2];
    monad::io::Ring &uring_;
    monad::io::Buffers &rwbuf_;
    monad::io::BufferPool rd_pool_;
    monad::io::BufferPool wr_pool_;

    // IO records
    IORecord records_;

    void submit_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data);
    void submit_request(
        std::span<const std::byte> buffer, file_offset_t offset,
        void *uring_data);

    bool poll_uring(bool blocking);

public:
    AsyncIO(
        std::pair<int, int> fds, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf)
        : fds_{fds.first, fds.second}
        , uring_(ring)
        , rwbuf_(rwbuf)
        , rd_pool_(monad::io::BufferPool(rwbuf, true))
        , wr_pool_(monad::io::BufferPool(rwbuf, false))
    {
        MONAD_ASSERT(fds_[WRITE] != -1);
        MONAD_ASSERT(fds_[READ] != -1);

        // register files
        MONAD_ASSERT(!io_uring_register_files(
            const_cast<io_uring *>(&uring_.get_ring()), fds_, 2));
    }
    AsyncIO(
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
    AsyncIO(
        use_anonymous_inode_tag, monad::io::Ring &ring,
        monad::io::Buffers &rwbuf)
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

    ~AsyncIO()
    {
        wait_until_done();
        MONAD_ASSERT(!records_.inflight);

        MONAD_ASSERT(!io_uring_unregister_files(
            const_cast<io_uring *>(&uring_.get_ring())));

        ::close(fds_[READ]);
        ::close(fds_[WRITE]);
    }

    unsigned io_in_flight() const noexcept
    {
        return records_.inflight > 0;
    }

    unsigned reads_in_flight() const noexcept
    {
        return records_.inflight_rd;
    }

    unsigned writes_in_flight() const noexcept
    {
        return records_.inflight - records_.inflight_rd;
    }

    // Blocks until at least one completion is processed, returning number
    // of completions processed.
    size_t poll_blocking(size_t count = 1)
    {
        size_t n = 0;
        for (; n < count && records_.inflight > 0; n++) {
            poll_uring(n == 0);
        }
        return n;
    }

    // Never blocks
    size_t poll_nonblocking(size_t count = size_t(-1))
    {
        size_t n = 0;
        for (; n < count && records_.inflight > 0; n++) {
            if (!poll_uring(false)) {
                break;
            }
        }
        return n;
    }

    void wait_until_done()
    {
        poll_blocking(size_t(-1));
    }

    void flush()
    {
        wait_until_done();
        records_.nreads = 0;
    }

    void submit_read_request(
        std::span<std::byte> buffer, file_offset_t offset, void *uring_data)
    {
        // get io_uring sqe, if no available entry, wait on poll() to reap
        // some
        while (records_.inflight >= uring_.get_sq_entries()) {
            poll_uring(true);
        }
        submit_request(buffer, offset, uring_data);
        ++records_.inflight;
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
        // get io_uring sqe, if no available entry, wait on poll() to reap
        // some
        while (records_.inflight >= uring_.get_sq_entries()) {
            poll_uring(true);
        }
        submit_request(buffer, offset, uring_data);
        ++records_.inflight;
    }

    /* This isn't the ideal place to put this, but only AsyncIO knows how to
    get i/o buffers into which to place connected i/o states.
    */
    static constexpr size_t MAX_CONNECTED_OPERATION_SIZE = DISK_PAGE_SIZE;
    static constexpr size_t READ_BUFFER_SIZE = round_up_align<DISK_PAGE_BITS>(
        uint16_t(MAX_DISK_NODE_SIZE + DISK_PAGE_SIZE));
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
            auto &io = p->executor();
            p->~erased_connected_operation();
#ifndef NDEBUG
            memset((void *)p, 0xff, MAX_CONNECTED_OPERATION_SIZE);
            memset((void *)buffer, 0xff, READ_BUFFER_SIZE);
#endif
            if (is_write) {
                io.wr_pool_.release(buffer);
            }
            else {
                io.rd_pool_.release(buffer);
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

static_assert(sizeof(AsyncIO) == 56);
static_assert(alignof(AsyncIO) == 8);

struct erased_connected_operation_deleter_io_receiver
{
    void set_value(
        erased_connected_operation *rawstate,
        result<std::span<const std::byte>> res)
    {
        MONAD_ASSERT(res);
        AsyncIO::erased_connected_operation_unique_ptr_type{rawstate};
    }
    void reset() {}
};

MONAD_TRIE_NAMESPACE_END
