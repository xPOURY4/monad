#pragma once

#include <monad/async/erased_connected_operation.hpp>

MONAD_ASYNC_NAMESPACE_BEGIN

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
}

MONAD_ASYNC_NAMESPACE_END
