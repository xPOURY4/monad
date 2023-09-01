#pragma once

#include <monad/async/concepts.hpp>

#include <monad/core/assert.h>

MONAD_ASYNC_NAMESPACE_BEGIN

class AsyncIO;
template <sender Sender, receiver Receiver>
class connected_operation;

namespace detail
{
    struct AsyncIO_per_thread_state_t;
};

enum class operation_type : uint8_t
{
    unknown,
    read,
    write,
    timeout,
    threadsafeop
};

/* \class erased_connected_operation
\brief A type erased abstract base class of a connected operation. Lets you
work with connection operation states with a type you are unaware of.
*/
class erased_connected_operation
{
public:
    template <sender Sender, receiver Receiver>
    friend class connected_operation;
    friend struct detail::AsyncIO_per_thread_state_t;

    enum class initiation_result
    {
        initiation_success,
        initiation_failed_told_receiver,
        deferred
    };

protected:
    operation_type _operation_type{operation_type::unknown};
    bool _being_executed{false};
    AsyncIO *_io{nullptr};
    erased_connected_operation *_next{nullptr};

    constexpr erased_connected_operation() {}

    constexpr erased_connected_operation(
        operation_type operation_type, AsyncIO &io)
        : _operation_type(operation_type)
        , _io(&io)
    {
    }

    virtual initiation_result
    _do_possibly_deferred_initiate(bool never_defer) noexcept = 0;

public:
    virtual ~erased_connected_operation()
    {
        MONAD_ASSERT(!_being_executed);
    }
    bool is_unknown_operation_type() const noexcept
    {
        return _operation_type == operation_type::unknown;
    }
    bool is_read() const noexcept
    {
        return _operation_type == operation_type::read;
    }
    bool is_write() const noexcept
    {
        return _operation_type == operation_type::write;
    }
    bool is_timeout() const noexcept
    {
        return _operation_type == operation_type::timeout;
    }
    bool is_threadsafeop() const noexcept
    {
        return _operation_type == operation_type::threadsafeop;
    }
    bool is_currently_being_executed() const noexcept
    {
        return _being_executed;
    }
    //! The executor instance being used, which may be none.
    AsyncIO *executor() noexcept
    {
        return _io;
    }
    //! Lets you store and access a pointer to a related connection operation.
    //! Usually used for forward linked lists.
    erased_connected_operation *&next() noexcept
    {
        return _next;
    }
    //! Invoke completion. The Sender will send the value to the Receiver. If
    //! the Receiver expects bytes transferred and the Sender does not send a
    //! value, terminates the program.
    virtual void completed(result<void> res) = 0;
    //! Invoke completion usually of an i/o, specifying the number of bytes
    //! transferred. If the Receiver does not expect bytes transferred, this
    //! will silently decay into the other `completed()` overload.
    virtual void completed(result<size_t> bytes_transferred) = 0;
    // Overload ambiguity resolver so you can write `completed(success())`
    // without ambiguous overload warnings.
    void completed(BOOST_OUTCOME_V2_NAMESPACE::success_type<void> _)
    {
        completed(result<void>(_));
    }
    //! Invoke initiation, sending any failure to the receiver
    initiation_result initiate() noexcept
    {
        return _do_possibly_deferred_initiate(false);
    }
    void reset() {}
};

MONAD_ASYNC_NAMESPACE_END
