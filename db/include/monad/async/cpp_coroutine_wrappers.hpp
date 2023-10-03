#pragma once

#include "io_worker_pool.hpp"

#include "detail/boost_outcome_coroutine_support.hpp"

#include <optional>

#ifndef MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER
//    #include <iostream>
//    #define MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(...) std::cout << "*** " <<
//    __VA_ARGS__ << std::endl;
    #define MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(...)
#endif

MONAD_ASYNC_NAMESPACE_BEGIN

//! \brief Concept matching an awaitable
template <class T>
concept awaitable = requires(T x, std::coroutine_handle<> h) {
    {
        x.await_ready()
    } -> std::same_as<bool>;
    x.await_suspend(h);
    x.await_resume();
};

namespace detail
{
    template <sender Sender>
    struct is_io_internal_buffer_sender_type : std::false_type
    {
    };
    template <>
    struct is_io_internal_buffer_sender_type<read_single_buffer_sender>
        : std::true_type
    {
    };
    template <>
    struct is_io_internal_buffer_sender_type<write_single_buffer_sender>
        : std::true_type
    {
    };

    template <
        sender Sender, receiver Receiver,
        bool = is_io_internal_buffer_sender_type<Sender>::value>
    struct connected_state_impl
    {
        using connected_state_type = decltype(connect(
            std::declval<AsyncIO &>(), std::declval<Sender>(),
            std::declval<Receiver>()));
        static connected_state_type make(AsyncIO &io, Sender &&sender)
        {
            return connect(io, std::move(sender), Receiver{});
        }
        template <class... SenderArgs>
        static connected_state_type make(
            AsyncIO &io, std::piecewise_construct_t _,
            std::tuple<SenderArgs...> &&sender_args)
        {
            return connect<Sender, Receiver>(
                io, _, std::move(sender_args), std::tuple{});
        }
        static connected_state_type &access(connected_state_type &v)
        {
            return v;
        }
        static const connected_state_type &access(const connected_state_type &v)
        {
            return v;
        }
    };
    template <sender Sender, receiver Receiver>
    struct connected_state_impl<Sender, Receiver, true>
    {
        using connected_state_type =
            AsyncIO::connected_operation_unique_ptr_type<Sender, Receiver>;
        static connected_state_type make(AsyncIO &io, Sender &&sender)
        {
            return io.make_connected(std::move(sender), Receiver{});
        }
        template <class... SenderArgs>
        static connected_state_type make(
            AsyncIO &io, std::piecewise_construct_t _,
            std::tuple<SenderArgs...> &&sender_args)
        {
            return io.make_connected<Sender, Receiver>(
                _, std::move(sender_args), std::tuple{});
        }
        static connected_state_type::element_type &
        access(connected_state_type &v)
        {
            return *v;
        }
        static const connected_state_type::element_type &
        access(const connected_state_type &v)
        {
            return *v;
        }
    };

    template <sender Sender>
    class awaitable
    {
        using _result_type = typename Sender::result_type;
        class _receiver_t
        {
            friend class awaitable;
            std::optional<_result_type> _result;
            std::coroutine_handle<> _cont;

        public:
            // We need AsyncIO to not recycle the i/o state until the future
            // gets destructed, so take over lifetime management outselves
            enum : bool
            {
                lifetime_managed_internally = false
            };

            // The receiver machinery
            void set_value(erased_connected_operation *, _result_type res)
            {
                assert(!_result.has_value());
                _result.emplace(std::move(res));
                if (_cont) {
                    MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(
                        this << " receiver resumes coroutine "
                             << _cont.address());
                    _cont.resume();
                }
            }
            void reset()
            {
                _result = {};
                _cont = {};
            }
        };
        using _impl = detail::connected_state_impl<Sender, _receiver_t>;
        using _connected_state_type = typename _impl::connected_state_type;

        _connected_state_type _state;

    public:
        template <class... Args>
        awaitable(Args &&...args)
            : _state(_impl::make(std::forward<Args>(args)...))
        {
            MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(
                &_impl::access(_state).receiver() << " initiates operation");
            _impl::access(_state).initiate();
        }

        bool await_ready() const noexcept
        {
            MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(
                &_impl::access(_state).receiver()
                << " await_ready = "
                << _impl::access(_state).receiver()._result.has_value());
            return _impl::access(_state).receiver()._result.has_value();
        }
        void await_suspend(std::coroutine_handle<> cont)
        {
            MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(
                &_impl::access(_state).receiver()
                << " await_suspend coroutine " << cont.address());
            assert(_impl::access(_state).receiver()._cont == nullptr);
            _impl::access(_state).receiver()._cont = cont;
        }
        _result_type await_resume()
        {
            MONAD_ASYNC_AWAITABLES_DEBUG_PRINTER(
                &_impl::access(_state).receiver() << " await_resume");
            assert(await_ready());
            return std::move(*_impl::access(_state).receiver()._result);
        }
    };
}

//! \brief Initiate the execution of a Sender on a specific `AsyncIO`
//! instance, returning an awaitable which can be `co_await`ed upon.
template <sender Sender>
[[nodiscard]] auto co_initiate(AsyncIO &io, Sender &&sender)
{
    return detail::awaitable<Sender>{io, std::move(sender)};
}

//! \brief Initiate the execution of a Sender on a specific `AsyncIO`
//! instance, returning an awaitable which can be `co_await`ed upon.
template <sender Sender, class... SenderArgs>
    requires(std::is_constructible_v<Sender, SenderArgs...>)
[[nodiscard]] auto co_initiate(
    AsyncIO &io, std::piecewise_construct_t _,
    std::tuple<SenderArgs...> &&sender_args)
{
    return detail::awaitable<Sender>{
        io, _, std::forward<SenderArgs>(sender_args)...};
}

//! \brief Convenience wrapper initiating a callable returning an awaitable
//! on a `` instance, returning an awaitable which readies when the callable
//! readies its awaitable on the worker thread.
template <class... QueueOptions, class F>
    requires(awaitable<std::invoke_result_t<F, erased_connected_operation *>>)
[[nodiscard]] auto
co_initiate(AsyncIO &io, AsyncReadIoWorkerPool<QueueOptions...> &pool, F f)
{
    using co_return_type =
        decltype(f(std::declval<erased_connected_operation *>())
                     .await_resume());
    struct invoke_coroutine_sender
    {
        using result_type = co_return_type;

        F f;
        std::optional<awaitables::eager<void>> aw;
        std::optional<co_return_type> res;

        invoke_coroutine_sender(F &&_f)
            : f(std::move(_f))
        {
        }
        result<void> operator()(erased_connected_operation *io_state) noexcept
        {
            aw.emplace(co_initiate(io_state));
            return success();
        }
        awaitables::eager<void>
        co_initiate(erased_connected_operation *io_state)
        {
            res = co_await f(io_state);
            io_state->completed(success());
            co_return;
        }
        result_type
        completed(erased_connected_operation *, result<void>) noexcept
        {
            return std::move(res).value();
        }
    };
    return detail::awaitable<execute_on_worker_pool<invoke_coroutine_sender>>{
        io,
        execute_on_worker_pool<invoke_coroutine_sender>(pool, std::move(f))};
}

//! \brief Suspend execution on the current `AsyncIO`, and resume execution on
//! the specified `AsyncIO`
[[nodiscard]] inline auto co_resume_execution_upon(AsyncIO &io)
{
    class awaitable
    {
        class _receiver_t
        {
            friend class awaitable;
            std::atomic<bool> _ready{false};
            std::optional<result<void>> _result;
            std::coroutine_handle<> _cont;

        public:
            // We need AsyncIO to not recycle the i/o state until the future
            // gets destructed, so take over lifetime management outselves
            enum : bool
            {
                lifetime_managed_internally = false
            };

            // The receiver machinery
            void set_value(erased_connected_operation *, result<void> res)
            {
                assert(!_result.has_value());
                _result.emplace(std::move(res));
                _ready.store(true, std::memory_order_release);
                if (_cont) {
                    _cont.resume();
                }
            }
            void reset()
            {
                _result = {};
                _cont = {};
            }
        };
        using _impl =
            detail::connected_state_impl<threadsafe_sender, _receiver_t>;
        using _connected_state_type = typename _impl::connected_state_type;

        _connected_state_type _state;

    public:
        awaitable(AsyncIO &io)
            : _state(_impl::make(io, std::piecewise_construct, std::tuple{}))
        {
        }

        bool await_ready() const noexcept
        {
            return _impl::access(_state).receiver()._ready.load(
                std::memory_order_acquire);
        }
        void await_suspend(std::coroutine_handle<> cont)
        {
            assert(_impl::access(_state).receiver()._cont == nullptr);
            _impl::access(_state).receiver()._cont = cont;
            _impl::access(_state).initiate();
        }
        result<void> await_resume()
        {
            assert(await_ready());
            return std::move(*_impl::access(_state).receiver()._result);
        }
    };
    return awaitable{io};
}

MONAD_ASYNC_NAMESPACE_END
