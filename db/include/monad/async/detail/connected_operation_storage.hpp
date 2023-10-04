#pragma once

#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/sender_errc.hpp>

MONAD_ASYNC_NAMESPACE_BEGIN

namespace detail
{
    struct AsyncIO_per_thread_state_t
    {
        AsyncIO *instance{nullptr};
        int within_completions_count{0};
        struct
        {
            erased_connected_operation *first{nullptr}, *last{nullptr};
        } pending_initiations;

        struct within_completions_holder;
        within_completions_holder enter_completions();
        bool empty() const noexcept
        {
            return pending_initiations.first == nullptr;
        }
        bool am_within_completions() const noexcept
        {
            assert(within_completions_count >= 0);
            return within_completions_count > 0;
        }
        bool if_within_completions_add_to_pending_initiations(
            erased_connected_operation *op)
        {
            erased_connected_operation::rbtree_node_traits::set_parent(
                op,
                nullptr); // use parent to store pointer to next item
            if (!am_within_completions()) {
                within_completions_reached_zero();
                return false;
            }
            if (pending_initiations.first == nullptr) {
                pending_initiations.first = pending_initiations.last = op;
                return true;
            }
            erased_connected_operation::rbtree_node_traits::set_parent(
                pending_initiations.last, op); // use parent to store pointer to
                                               // next item
            pending_initiations.last = op;
            return true;
        }
        void within_completions_reached_zero()
        {
            if (pending_initiations.first != nullptr) {
                within_completions_count++;
                auto *original_last = pending_initiations.last;
                while (pending_initiations.first != nullptr) {
                    erased_connected_operation *op = pending_initiations.first;
                    pending_initiations.first =
                        erased_connected_operation::rbtree_node_traits::
                            get_parent(pending_initiations.first);
                    if (pending_initiations.first == nullptr) {
                        pending_initiations.last = nullptr;
                    }
                    op->_do_possibly_deferred_initiate(true);
                    if (op == original_last) {
                        // Prevent infinite loops caused by initiations adding
                        // more stuff to pending initiations
                        break;
                    }
                }
                within_completions_count--;
            }
        }
    };
    // Implemented in io.cpp
    extern __attribute__((visibility("default"))) AsyncIO_per_thread_state_t &
    AsyncIO_per_thread_state();
    inline AsyncIO *AsyncIO_thread_instance() noexcept
    {
        auto &ts = AsyncIO_per_thread_state();
        return ts.instance;
    }

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
            if (!res) {
                completed(result<void>(std::move(res).as_failure()));
            }
            else {
                completed(result<void>(success()));
            }
        }

    public:
        using initiation_result = typename Base::initiation_result;
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
        static constexpr operation_type _operation_type = []() constexpr {
            if constexpr (requires { Sender::my_operation_type; }) {
                return Sender::my_operation_type;
            }
            else if constexpr (requires {
                                   typename Sender::buffer_type::element_type;
                               }) {
                constexpr bool is_const =
                    std::is_const_v<typename Sender::buffer_type::element_type>;
                return is_const ? operation_type::write : operation_type::read;
            }
            else {
                return operation_type::unknown;
            }
        }();

        virtual initiation_result
        _do_possibly_deferred_initiate(bool never_defer) noexcept override
        {
            this->_being_executed = true;
            // Prevent compiler reordering write of _being_executed after this
            // point without using actual atomics.
            std::atomic_signal_fence(std::memory_order_release);
            auto *thisio = this->_io.load(std::memory_order_acquire);
            if (!never_defer &&
                AsyncIO_per_thread_state()
                    .if_within_completions_add_to_pending_initiations(this)) {
                return initiation_result::deferred;
            }
            auto r = _sender(this);
            if (!r) {
                this->_being_executed = false;
                if (r.assume_error() == MONAD_ASYNC_NAMESPACE::sender_errc::
                                            initiation_immediately_completed) {
                    sender_errc_code sec(std::move(r).assume_error());
                    size_t bytes_transferred = sec.value().value;
                    completed(bytes_transferred);
                    return initiation_result::initiation_immediately_completed;
                }
                else {
                    completed(std::move(r));
                    return initiation_result::initiation_failed_told_receiver;
                }
            }
            if (thisio != nullptr) {
                thisio->_notify_operation_initiation_success(this);
            }
            return initiation_result::initiation_success;
        }

    public:
        connected_operation_storage() = default;
        connected_operation_storage(
            sender_type &&sender, receiver_type &&receiver)
            : _sender(static_cast<Sender &&>(sender))
            , _receiver(static_cast<Receiver &&>(receiver))
        {
        }
        connected_operation_storage(
            AsyncIO &io, bool lifetime_managed_internally, sender_type &&sender,
            receiver_type &&receiver)
            : erased_connected_operation(
                  _operation_type, io, lifetime_managed_internally)
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
            AsyncIO &io, bool lifetime_managed_internally,
            std::piecewise_construct_t, std::tuple<SenderArgs...> sender_args,
            std::tuple<ReceiverArgs...> receiver_args)
            : erased_connected_operation(
                  _operation_type, io, lifetime_managed_internally)
            , _sender(std::make_from_tuple<Sender>(std::move(sender_args)))
            , _receiver(
                  std::make_from_tuple<Receiver>(std::move(receiver_args)))
        {
        }

        connected_operation_storage(connected_operation_storage const &) =
            delete;
        connected_operation_storage(connected_operation_storage &&) = delete;
        connected_operation_storage &
        operator=(connected_operation_storage const &) = delete;
        connected_operation_storage &
        operator=(connected_operation_storage &&) = delete;
        ~connected_operation_storage() = default;

        sender_type &sender() & noexcept
        {
            return _sender;
        }
        const sender_type &sender() const & noexcept
        {
            return _sender;
        }
        sender_type sender() && noexcept
        {
            return static_cast<sender_type &&>(_sender);
        }
        const sender_type sender() const && noexcept
        {
            return static_cast<sender_type &&>(_sender);
        }
        const receiver_type &receiver() const & noexcept
        {
            return _receiver;
        }
        receiver_type &receiver() & noexcept
        {
            return _receiver;
        }
        receiver_type receiver() && noexcept
        {
            return static_cast<receiver_type &&>(_receiver);
        }
        const receiver_type receiver() const && noexcept
        {
            return static_cast<const receiver_type &&>(_receiver);
        }

        static constexpr bool is_unknown_operation_type() noexcept
        {
            return _operation_type == operation_type::unknown;
        }
        static constexpr bool is_read() noexcept
        {
            return _operation_type == operation_type::read;
        }
        static constexpr bool is_write() noexcept
        {
            return _operation_type == operation_type::write;
        }
        static constexpr bool is_timeout() noexcept
        {
            return _operation_type == operation_type::timeout;
        }

        //! Initiates the operation, calling the Receiver with any failure,
        //! returning if deferred or if immediate failure occurred.
        //! If successful do NOT modify anything after
        //! this until after completion, it may cause a silent page
        //! copy-on-write.
        initiation_result initiate() noexcept
        {
            // You must initiate operations on the same kernel thread as
            // the AsyncIO instance associated with this operation state
            // (except for threadsafeop)
            auto *thisio = this->executor();
            MONAD_ASSERT(
                thisio == nullptr || this->is_threadsafeop() ||
                thisio->owning_thread_id() == gettid());
            // The threadsafe op is special, it isn't for this AsyncIO instance
            // and therefore never needs deferring
            return this->_do_possibly_deferred_initiate(
                this->is_threadsafeop());
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
            auto *thisio = this->executor();
            if (thisio != nullptr) {
                thisio->_notify_operation_reset(this);
            }
        }
    };
}

MONAD_ASYNC_NAMESPACE_END
