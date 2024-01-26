#pragma once

#include <monad/config.hpp>

#include <monad/core/unordered_map.hpp>

#include <boost/fiber/algo/round_robin.hpp>
#include <boost/fiber/fiber.hpp>

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#include <boost/fiber/future.hpp>
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

#include <iostream>
#include <mutex>

#define MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING 1

MONAD_NAMESPACE_BEGIN

/*! \brief A threadsafe `boost::fibers::promise`.

Rather annoyingly when using Boost.Fibers promises across kernel threads,
if you destroy the promise in the awoken kernel thread before the kernel
thread setting the value is done with the promise, you get a segfault.
This deeply unhelpful behaviour is worked around using an atomic to signal
when the code setting the promise has finished, and thus it is now safe
to destroy the promise.
*/
template <class T>
class threadsafe_boost_fibers_promise
{
    ::boost::fibers::promise<T> promise_;
    std::atomic<pid_t> promise_can_be_destroyed_{-1};

public:
    threadsafe_boost_fibers_promise() {}

    threadsafe_boost_fibers_promise(threadsafe_boost_fibers_promise const &) =
        delete;
    threadsafe_boost_fibers_promise(threadsafe_boost_fibers_promise &&) =
        delete;
    threadsafe_boost_fibers_promise &
    operator=(threadsafe_boost_fibers_promise const &) = delete;
    threadsafe_boost_fibers_promise &
    operator=(threadsafe_boost_fibers_promise &&) = delete;

    ~threadsafe_boost_fibers_promise()
    {
        for (;;) {
            auto const tid =
                promise_can_be_destroyed_.load(std::memory_order_acquire);
            if (tid < 0 || tid == gettid()) {
                break;
            }
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
            // std::cerr << "promise " << this << " is awaiting can be
            // destroyed"
            //           << std::endl;
#endif
            ::boost::this_fiber::yield();
        }
    }

    void reset()
    {
        promise_ = {};
        promise_can_be_destroyed_.store(-1, std::memory_order_release);
    }

    auto get_future()
    {
        auto ret = promise_.get_future();
        promise_can_be_destroyed_.store(-2, std::memory_order_release);
        return ret;
    }

    void set_exception(std::exception_ptr p)
    {
        promise_can_be_destroyed_.store(gettid(), std::memory_order_release);
        promise_.set_exception(std::move(p));
        promise_can_be_destroyed_.store(-3, std::memory_order_release);
    }

    void set_value(T const &v)
    {
        promise_can_be_destroyed_.store(gettid(), std::memory_order_release);
        promise_.set_value(v);
        promise_can_be_destroyed_.store(-3, std::memory_order_release);
    }

    void set_value(T &&v)
    {
        promise_can_be_destroyed_.store(gettid(), std::memory_order_release);
        promise_.set_value(std::move(v));
        promise_can_be_destroyed_.store(-3, std::memory_order_release);
    }
};

template <>
class threadsafe_boost_fibers_promise<void>
{
    ::boost::fibers::promise<void> promise_;
    std::atomic<pid_t> promise_can_be_destroyed_{-1};

public:
    threadsafe_boost_fibers_promise() {}

    threadsafe_boost_fibers_promise(threadsafe_boost_fibers_promise const &) =
        delete;
    threadsafe_boost_fibers_promise(threadsafe_boost_fibers_promise &&) =
        delete;
    threadsafe_boost_fibers_promise &
    operator=(threadsafe_boost_fibers_promise const &) = delete;
    threadsafe_boost_fibers_promise &
    operator=(threadsafe_boost_fibers_promise &&) = delete;

    ~threadsafe_boost_fibers_promise()
    {
        for (;;) {
            auto const tid =
                promise_can_be_destroyed_.load(std::memory_order_acquire);
            if (tid < 0 || tid == gettid()) {
                break;
            }
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
            // std::cerr << "promise " << this << " is awaiting can be
            // destroyed"
            //           << std::endl;
#endif
            ::boost::this_fiber::yield();
        }
    }

    auto get_future()
    {
        auto ret = promise_.get_future();
        promise_can_be_destroyed_.store(-2, std::memory_order_release);
        return ret;
    }

    void set_exception(std::exception_ptr p)
    {
        promise_can_be_destroyed_.store(gettid(), std::memory_order_release);
        promise_.set_exception(std::move(p));
        promise_can_be_destroyed_.store(-3, std::memory_order_release);
    }

    void set_value()
    {
        promise_can_be_destroyed_.store(gettid(), std::memory_order_release);
        promise_.set_value();
        promise_can_be_destroyed_.store(-3, std::memory_order_release);
    }
};

namespace detail
{
    struct debugging_fiber_scheduler_algorithm_wrapper_shared_state_t
    {
        std::mutex lock;
        unordered_dense_map<pid_t, unordered_flat_set<boost::fibers::context *>>
            tid_to_fibers;
        unordered_flat_map<boost::fibers::context *, pid_t> fiber_to_tid;
    };

    extern inline __attribute__((visibility("default")))
    debugging_fiber_scheduler_algorithm_wrapper_shared_state_t &
    debugging_fiber_scheduler_algorithm_wrapper_shared_state()
    {
        static debugging_fiber_scheduler_algorithm_wrapper_shared_state_t v;
        return v;
    }
}

/*! \brief Non-hanging Boost.Fiber scheduler

When multiple kernel threads use Boost.Fiber objects, you can get random
hangs. This custom Fiber scheduler works around those issues.
*/
template <class BaseFiberScheduler>
class debugging_fiber_scheduler_algorithm_wrapper : public BaseFiberScheduler
{
    virtual void awakened(boost::fibers::context *ctx) noexcept override
    {
        {
            auto const mytid = gettid();
            auto &state = detail::
                debugging_fiber_scheduler_algorithm_wrapper_shared_state();
            std::lock_guard g(state.lock);
            auto it = state.fiber_to_tid.find(ctx);
            if (it == state.fiber_to_tid.end()) {
                state.fiber_to_tid[ctx] = mytid;
                state.tid_to_fibers[mytid].insert(ctx);
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
                std::cerr << "awakened(): Boost fiber " << ctx
                          << " is awakened for first time on thread " << mytid
                          << std::endl;
#endif
            }
            else if (it->second != mytid) {
                // fiber has changed kernel thread
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
                std::cerr << "awakened(): Boost fiber " << ctx
                          << " is moved from thread " << it->second
                          << " to thread " << mytid << std::endl;
#endif
                state.tid_to_fibers[it->second].erase(ctx);
                state.fiber_to_tid[ctx] = mytid;
                state.tid_to_fibers[mytid].insert(ctx);
            }
            else {
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
                std::cerr << "awakened(): Boost fiber " << ctx
                          << " is resumed on thread " << mytid << std::endl;
#endif
            }
        }
        BaseFiberScheduler::awakened(ctx);
    }

    virtual boost::fibers::context *pick_next() noexcept override
    {
        auto *ctx = BaseFiberScheduler::pick_next();
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
        auto const mytid = gettid();
        auto &state =
            detail::debugging_fiber_scheduler_algorithm_wrapper_shared_state();
        std::lock_guard g(state.lock);
        std::cerr << "pick_next(): Boost fiber " << ctx
                  << " is picked for thread " << mytid << std::endl;
#endif
        return ctx;
    }

    virtual bool has_ready_fibers() const noexcept override
    {
        return BaseFiberScheduler::has_ready_fibers();
    }

    virtual void suspend_until(
        std::chrono::steady_clock::time_point const &tm) noexcept override
    {
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
        auto const mytid = gettid();
        auto &state =
            detail::debugging_fiber_scheduler_algorithm_wrapper_shared_state();
        std::lock_guard g(state.lock);
        std::cerr << "suspend_until(): for thread " << mytid << std::endl;
#endif
        BaseFiberScheduler::suspend_until(tm);
    }

    virtual void notify() noexcept override
    {
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
        auto const mytid = gettid();
        auto &state =
            detail::debugging_fiber_scheduler_algorithm_wrapper_shared_state();
        std::lock_guard g(state.lock);
        std::cerr << "notify(): for thread " << mytid << std::endl;
#endif
        BaseFiberScheduler::notify();
    }

public:
    debugging_fiber_scheduler_algorithm_wrapper()
    {
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
        auto const mytid = gettid();
        auto &state =
            detail::debugging_fiber_scheduler_algorithm_wrapper_shared_state();
        std::lock_guard g(state.lock);
        std::cerr << "Fiber scheduler constructs for thread " << mytid
                  << std::endl;
#endif
    }

    ~debugging_fiber_scheduler_algorithm_wrapper()
    {
        auto const mytid = gettid();
        auto &state =
            detail::debugging_fiber_scheduler_algorithm_wrapper_shared_state();
        std::lock_guard g(state.lock);
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
        std::cerr << "Fiber scheduler destructs for thread " << mytid
                  << std::endl;
#endif
        auto it = state.tid_to_fibers.find(mytid);
        if (it != state.tid_to_fibers.end()) {
            for (auto *ctx : it->second) {
#if MONAD_BOOST_FIBER_WORKAROUNDS_DEBUG_PRINTING
                std::cerr << "   Fiber " << ctx << " is detached" << std::endl;
#endif
                state.fiber_to_tid.erase(ctx);
            }
            state.tid_to_fibers.erase(it);
        }
    }
};

template <class BaseFiberScheduler = ::boost::fibers::algo::round_robin>
inline void use_debugging_fiber_scheduler_wrapper()
{
    ::boost::fibers::use_scheduling_algorithm<
        debugging_fiber_scheduler_algorithm_wrapper<BaseFiberScheduler>>();
}

MONAD_NAMESPACE_END
