#pragma once

#include <monad/async/concepts.hpp>

#include <monad/core/assert.h>

#include <boost/intrusive/rbtree_algorithms.hpp>

MONAD_ASYNC_NAMESPACE_BEGIN

class AsyncIO;
template <sender Sender, receiver Receiver>
class connected_operation;

namespace detail
{
    struct AsyncIO_per_thread_state_t;
    template <class QueueOptions>
    class AsyncReadIoWorkerPoolImpl;
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
    template <class QueueOptions>
    friend class detail::AsyncReadIoWorkerPoolImpl;
    friend struct detail::AsyncIO_per_thread_state_t;

    enum class initiation_result
    {
        initiation_success,
        initiation_failed_told_receiver,
        initiation_immediately_completed,
        deferred
    };

protected:
    operation_type _operation_type{operation_type::unknown};
    bool _being_executed{false};
    bool _lifetime_managed_internally{
        false}; // some factory classes may deallocate states on their own
    std::atomic<AsyncIO *> _io{
        nullptr}; // set at construction if associated with an AsyncIO instance,
                  // which isn't mandatory
    struct _rbtree_t
    {
        /* Users of these fields:

           - `parent` gets used by `AsyncIO_per_thread_state` to keep a forward
           list of operations to be initiated when the thread stack unwinds. It
           stops using it before initiation.

           - i/o read uses `key` between initiation and completion. It says what
           offset to add to bytes transferred returned.

           - i/o write uses all these fields between initiation and completion.
           `key` is the offset at which the write is being performed.
        */
        union
        {
            _rbtree_t *parent{nullptr};
            erased_connected_operation *parent_;
        };
        _rbtree_t *left{nullptr}, *right{nullptr};
        file_offset_t key : 63 {0};
        file_offset_t color : 1 {false};
    } _rbtree;

    constexpr erased_connected_operation() {}

    constexpr erased_connected_operation(
        operation_type operation_type, AsyncIO &io,
        bool lifetime_managed_internally)
        : _operation_type(operation_type)
        , _lifetime_managed_internally(lifetime_managed_internally)
        , _io(&io)
    {
#ifndef __clang__
        assert(&io != nullptr);
#endif
    }

    virtual initiation_result
    _do_possibly_deferred_initiate(bool never_defer) noexcept = 0;

public:
    struct rbtree_node_traits
    {
        using node = _rbtree_t;
        using node_ptr = _rbtree_t *;
        using const_node_ptr = _rbtree_t const *;
        using color = bool;
        static node_ptr get_parent(const_node_ptr n)
        {
            return n->parent;
        }
        static void set_parent(node_ptr n, node_ptr parent)
        {
            n->parent = parent;
        }
        static node_ptr get_left(const_node_ptr n)
        {
            return n->left;
        }
        static void set_left(node_ptr n, node_ptr left)
        {
            n->left = left;
        }
        static node_ptr get_right(const_node_ptr n)
        {
            return n->right;
        }
        static void set_right(node_ptr n, node_ptr right)
        {
            n->right = right;
        }
        static color get_color(const_node_ptr n)
        {
            return n->color;
        }
        static void set_color(node_ptr n, color c)
        {
            n->color = c;
        }
        static color black()
        {
            return color(false);
        }
        static color red()
        {
            return color(true);
        }
        static file_offset_t get_key(const_node_ptr n)
        {
            return n->key;
        }
        static void set_key(node_ptr n, file_offset_t v)
        {
            n->key = v;
            assert(n->key == v);
        }
        static erased_connected_operation *
        get_parent(erased_connected_operation const *n)
        {
            return n->_rbtree.parent_;
        }
        static void set_parent(
            erased_connected_operation *n, erased_connected_operation *parent)
        {
            n->_rbtree.parent_ = parent;
        }
        static file_offset_t get_key(erased_connected_operation const *n)
        {
            return n->_rbtree.key;
        }
        static void set_key(erased_connected_operation *n, file_offset_t v)
        {
            n->_rbtree.key = v;
            assert(n->_rbtree.key == v);
        }
        static node_ptr to_node_ptr(erased_connected_operation *n)
        {
            return &n->_rbtree;
        }
        static const_node_ptr to_node_ptr(erased_connected_operation const *n)
        {
            return &n->_rbtree;
        }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored                                                 \
    "-Winvalid-offsetof" // complains about lack of standard layout
        static erased_connected_operation *
        to_erased_connected_operation(node_ptr n)
        {
            return reinterpret_cast<erased_connected_operation *>(
                ((char *)n) - offsetof(erased_connected_operation, _rbtree));
        }
        static erased_connected_operation const *
        to_erased_connected_operation(const_node_ptr n)
        {
            return reinterpret_cast<erased_connected_operation const *>(
                ((char const *)n) -
                offsetof(erased_connected_operation, _rbtree));
        }
#pragma GCC diagnostic pop
    };
    friend struct rbtree_node_traits;

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
    bool lifetime_is_managed_internally() const noexcept
    {
        return _lifetime_managed_internally;
    }
    //! The executor instance being used, which may be none.
    AsyncIO *executor() noexcept
    {
        return _io.load(std::memory_order_acquire);
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
static_assert(sizeof(erased_connected_operation) == 56);
static_assert(alignof(erased_connected_operation) == 8);

MONAD_ASYNC_NAMESPACE_END
