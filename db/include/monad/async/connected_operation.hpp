#pragma once

#include <monad/async/detail/connected_operation_storage.hpp>

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

MONAD_ASYNC_NAMESPACE_BEGIN

namespace detail
{
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
            if (this->_io != nullptr) {
                this->_io->_notify_operation_completed(this, res);
            }
            if constexpr (requires(Sender x) {
                              x.completed(this, std::move(res));
                          }) {
                auto r = this->_sender.completed(this, std::move(res));
                [[unlikely]] if (
                    !r && r.assume_error() ==
                              sender_errc::operation_must_be_reinitiated) {
                    this->initiate();
                }
                else {
                    this->_receiver.set_value(this, std::move(r));
                }
            }
            else {
                this->_receiver.set_value(this, std::move(res));
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
            if (this->_io != nullptr) {
                this->_io->_notify_operation_completed(this, bytes_transferred);
            }
            if constexpr (requires(Sender x) {
                              x.completed(this, std::move(bytes_transferred));
                          }) {
                auto r =
                    this->_sender.completed(this, std::move(bytes_transferred));
                [[unlikely]] if (
                    !r && r.assume_error() ==
                              sender_errc::operation_must_be_reinitiated) {
                    this->initiate();
                }
                else {
                    this->_receiver.set_value(this, std::move(r));
                }
            }
            else {
                this->_receiver.set_value(this, std::move(bytes_transferred));
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

    // This is an immovable in memory object
    connected_operation(connected_operation const &) = delete;
    connected_operation(connected_operation &&) = delete;
    connected_operation &operator=(connected_operation const &) = delete;
    connected_operation &operator=(connected_operation &&) = delete;
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
        io,
        [] {
            if constexpr (requires { Receiver::lifetime_managed_internally; }) {
                return Receiver::lifetime_managed_internally;
            }
            else {
                return true;
            }
        }(),
        static_cast<Sender &&>(sender),
        static_cast<Receiver &&>(receiver));
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
        io,
        [] {
            if constexpr (requires { Receiver::lifetime_managed_internally; }) {
                return Receiver::lifetime_managed_internally;
            }
            else {
                return true;
            }
        }(),
        _,
        std::move(sender_args),
        std::move(receiver_args));
}

MONAD_ASYNC_NAMESPACE_END
