#include "gtest/gtest.h"

#include "monad/async/io_senders.hpp"
#include "monad/core/small_prng.hpp"

#include <boost/fiber/fiber.hpp>
#include <boost/fiber/future.hpp>
#include <boost/outcome/coroutine_support.hpp>

#include <array>
#include <chrono>
#include <coroutine>
#include <optional>
#include <span>
#include <vector>

namespace
{
    using namespace MONAD_ASYNC_NAMESPACE;
    static constexpr size_t TEST_FILE_SIZE = 1024 * 1024;
    static constexpr size_t MAX_CONCURRENCY = 4;
    static const std::vector<std::byte> testfilecontents = [] {
        std::vector<std::byte> ret(TEST_FILE_SIZE);
        std::span<
            monad::small_prng::value_type,
            TEST_FILE_SIZE / sizeof(monad::small_prng::value_type)>
            s((monad::small_prng::value_type *)ret.data(),
              TEST_FILE_SIZE / sizeof(monad::small_prng::value_type));
        monad::small_prng rand;
        for (auto &i : s) {
            i = rand();
        }
        return ret;
    }();
    static monad::io::Ring testring(MAX_CONCURRENCY * 2, 0);
    static monad::io::Buffers testrwbuf{
        testring, MAX_CONCURRENCY * 2, MAX_CONCURRENCY * 2, 1UL << 13};
    static auto testio = [] {
        auto ret = std::make_unique<AsyncIO>(
            use_anonymous_inode_tag{}, testring, testrwbuf);
        MONAD_ASSERT(
            TEST_FILE_SIZE ==
            ::write(ret->get_rd_fd(), testfilecontents.data(), TEST_FILE_SIZE));
        return ret;
    }();
    monad::small_prng test_rand;

    struct read_single_buffer_operation_states_base
    {
        virtual bool reinitiate(
            erased_connected_operation *i,
            std::span<const std::byte> buffer) = 0;
    };
    template <receiver Receiver>
    class read_single_buffer_operation_states final
        : public read_single_buffer_operation_states_base
    {
        using _io_state_type = AsyncIO::connected_operation_unique_ptr_type<
            read_single_buffer_sender, Receiver>;
        std::vector<_io_state_type> _states;
        std::vector<std::array<std::byte, DISK_PAGE_SIZE>> _buffers;
        bool _test_is_done{false};
        unsigned _op_count{0};

    public:
        explicit read_single_buffer_operation_states(size_t total)
            : _buffers(total)
        {
            _states.reserve(total);
            for (size_t n = 0; n < total; n++) {
                auto offset = round_down_align<DISK_PAGE_BITS>(
                    test_rand() % (TEST_FILE_SIZE - DISK_PAGE_SIZE));
                _states.push_back(testio->make_connected(
                    read_single_buffer_sender(
                        offset, {_buffers[n].data(), DISK_PAGE_SIZE}),
                    Receiver{this}));
            }
        }
        ~read_single_buffer_operation_states()
        {
            stop();
        }
        unsigned count() const noexcept
        {
            return _op_count;
        }
        void initiate()
        {
            _test_is_done = false;
            for (auto &i : _states) {
                i->initiate();
            }
            _op_count = _states.size();
        }
        void stop()
        {
            _test_is_done = true;
            testio->wait_until_done();
        }
        virtual bool reinitiate(
            erased_connected_operation *i,
            std::span<const std::byte> buffer) override final
        {
            auto *state = static_cast<typename _io_state_type::pointer>(i);
            EXPECT_EQ(
                buffer.front(), testfilecontents[state->sender().offset()]);
            if (!_test_is_done) {
                auto offset = round_down_align<DISK_PAGE_BITS>(
                    test_rand() % (TEST_FILE_SIZE - DISK_PAGE_SIZE));
                state->reset(
                    std::tuple{offset, state->sender().buffer()}, std::tuple{});
                state->initiate();
                _op_count++;
                return true;
            }
            return false;
        }
        read_single_buffer_sender &sender(size_t idx) noexcept
        {
            return _states[idx]->sender();
        }
        Receiver &receiver(size_t idx) noexcept
        {
            return _states[idx]->receiver();
        }
    };

    TEST(AsyncIO, timed_delay_sender_receiver)
    {
        auto check = [](const char *desc, auto &&get_now, auto timeout) {
            struct receiver_t
            {
                bool done{false};
                void set_value(erased_connected_operation *, result<void> res)
                {
                    ASSERT_TRUE(res);
                    done = true;
                }
            };
            auto state =
                connect(*testio, timed_delay_sender(timeout), receiver_t{});
            std::cout << "   " << desc << " ..." << std::endl;
            auto begin = get_now();
            state.initiate();
            while (!state.receiver().done) {
                testio->poll_blocking(1);
            }
            auto end = get_now();
            std::cout << "      io_uring waited for "
                      << (std::chrono::duration_cast<std::chrono::microseconds>(
                              end - begin)
                              .count() /
                          1000.0)
                      << " ms." << std::endl;
            if constexpr (requires { timeout.count(); }) {
                auto diff = end - begin;
                EXPECT_GE(diff, timeout);
                EXPECT_LT(diff, timeout + std::chrono::milliseconds(100));
            }
            else {
                EXPECT_GE(end, timeout);
                EXPECT_LT(end, timeout + std::chrono::milliseconds(100));
            }
        };
        check(
            "Relative delay",
            [] { return std::chrono::steady_clock::now(); },
            std::chrono::milliseconds(100));
        check(
            "Absolute monotonic deadline",
            [] { return std::chrono::steady_clock::now(); },
            std::chrono::steady_clock::now() + std::chrono::milliseconds(100));
        check(
            "Absolute UTC deadline",
            [] { return std::chrono::system_clock::now(); },
            std::chrono::system_clock::now() + std::chrono::milliseconds(100));
        check(
            "Instant",
            [] { return std::chrono::steady_clock::now(); },
            std::chrono::milliseconds(0));
    }

    /* A receiver which just immediately asks the sender
    to reinitiate the i/o. This test models traditional
    completion handler based i/o.
    */
    struct completion_handler_io_receiver
    {
        read_single_buffer_operation_states_base *state;

        explicit completion_handler_io_receiver(
            read_single_buffer_operation_states_base *s)
            : state(s)
        {
        }
        void set_value(
            erased_connected_operation *rawstate,
            result<std::span<const std::byte>> buffer)
        {
            ASSERT_TRUE(buffer);
            state->reinitiate(rawstate, buffer.assume_value());
        }
        void reset() {}
    };

    TEST(AsyncIO, completion_handler_sender_receiver)
    {
        read_single_buffer_operation_states<completion_handler_io_receiver>
            states(MAX_CONCURRENCY);
        auto begin = std::chrono::steady_clock::now(), end = begin;
        states.initiate();
        for (; end - begin < std::chrono::seconds(5);
             end = std::chrono::steady_clock::now()) {
            testio->poll_blocking(256);
        }
        states.stop();
        end = std::chrono::steady_clock::now();
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        std::cout << "Did " << (1000.0 * states.count() / diff.count())
                  << " random single byte reads per second from file length "
                  << (TEST_FILE_SIZE / 1024 / 1024) << " Mb" << std::endl;
    }

#if __GNUC__ == 12
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    /* A receiver which suspends and resumes a C++ coroutine
     */
    struct cpp_suspend_resume_io_receiver
    {
        using result_type = std::pair<
            erased_connected_operation *, result<std::span<const std::byte>>>;
        std::coroutine_handle<> _h;
        std::optional<result_type> res;

        explicit cpp_suspend_resume_io_receiver(
            read_single_buffer_operation_states_base *)
        {
        }
        void set_value(
            erased_connected_operation *rawstate,
            result<std::span<const std::byte>> buffer)
        {
            assert(!res.has_value());
            res = {rawstate, std::move(buffer)};
            _h.resume();
        }
        void reset()
        {
            _h = {};
            res.reset();
        }

        // This is the C++ coroutine machinery which declares
        // that this type is an awaitable
        bool await_ready() const noexcept
        {
            return res.has_value();
        }
        void await_suspend(std::coroutine_handle<> h)
        {
            assert(!res.has_value());
            _h = h;
        }
        result_type await_resume()
        {
            assert(res.has_value());
            auto ret = std::move(res).value();
            res.reset();
            return ret;
        }
    };
#if __GNUC__ == 12
    #pragma GCC diagnostic pop
#endif

    TEST(AsyncIO, cpp_coroutine_sender_receiver)
    {
        read_single_buffer_operation_states<cpp_suspend_resume_io_receiver>
            states(MAX_CONCURRENCY);
        auto begin = std::chrono::steady_clock::now(), end = begin;
        auto coroutine = [&](cpp_suspend_resume_io_receiver &receiver)
            -> BOOST_OUTCOME_V2_NAMESPACE::awaitables::eager<void> {
            for (;;) {
                auto [rawstate, buffer] = co_await receiver;
                // Can't use gtest ASSERT_TRUE here as it is not coroutine
                // compatible
                if (!buffer) {
                    abort();
                }
                if (!states.reinitiate(rawstate, buffer.assume_value())) {
                    co_return;
                }
            }
        };
        std::vector<BOOST_OUTCOME_V2_NAMESPACE::awaitables::eager<void>>
            awaitables;
        awaitables.reserve(MAX_CONCURRENCY);
        for (size_t n = 0; n < MAX_CONCURRENCY; n++) {
            awaitables.push_back(coroutine(states.receiver(n)));
        }
        states.initiate();
        for (; end - begin < std::chrono::seconds(5);
             end = std::chrono::steady_clock::now()) {
            testio->poll_blocking(256);
        }
        states.stop();
        awaitables.clear();
        end = std::chrono::steady_clock::now();
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        std::cout << "Did " << (1000.0 * states.count() / diff.count())
                  << " random single byte reads per second from file length "
                  << (TEST_FILE_SIZE / 1024 / 1024) << " Mb" << std::endl;
    }

    /* A receiver which suspends and resumes a Boost.Fiber
     */
    struct fiber_suspend_resume_io_receiver
    {
        using result_type = std::pair<
            erased_connected_operation *, result<std::span<const std::byte>>>;
        boost::fibers::promise<result_type> promise;

        explicit fiber_suspend_resume_io_receiver(
            read_single_buffer_operation_states_base *)
        {
        }
        void set_value(
            erased_connected_operation *rawstate,
            result<std::span<const std::byte>> buffer)
        {
            promise.set_value({rawstate, std::move(buffer)});
        }
        void reset()
        {
            promise = {};
        }
    };

    TEST(AsyncIO, fiber_sender_receiver)
    {
        read_single_buffer_operation_states<fiber_suspend_resume_io_receiver>
            states(MAX_CONCURRENCY);
        auto begin = std::chrono::steady_clock::now(), end = begin;
        auto fiber = [&](fiber_suspend_resume_io_receiver *receiver) {
            for (;;) {
                auto future = receiver->promise.get_future();
                auto [rawstate, buffer] = future.get();
                ASSERT_TRUE(buffer);
                if (!states.reinitiate(rawstate, buffer.assume_value())) {
                    return;
                }
            }
        };
        std::vector<boost::fibers::fiber> fibers;
        fibers.reserve(MAX_CONCURRENCY);
        for (size_t n = 0; n < MAX_CONCURRENCY; n++) {
            fibers.emplace_back(
                boost::fibers::fiber(fiber, &states.receiver(n)));
        }
        states.initiate();
        for (; end - begin < std::chrono::seconds(5);
             end = std::chrono::steady_clock::now()) {
            boost::this_fiber::yield();
            testio->poll_blocking(256);
        }
        states.stop();
        for (auto &i : fibers) {
            i.join();
        }
        fibers.clear();
        end = std::chrono::steady_clock::now();
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin);
        std::cout << "Did " << (1000.0 * states.count() / diff.count())
                  << " random single byte reads per second from file length "
                  << (TEST_FILE_SIZE / 1024 / 1024) << " Mb" << std::endl;
    }

    TEST(AsyncIO, stack_overflow_avoided)
    {
        static constexpr size_t COUNT = 100000;
        struct receiver_t;
        static std::vector<std::unique_ptr<erased_connected_operation>> ops;
        static unsigned stack_depth = 0, counter = 0,
                        last_receiver_count = unsigned(-1);
        ops.reserve(COUNT);
        struct receiver_t
        {
            unsigned count;
            void set_value(erased_connected_operation *, result<void> res)
            {
                static thread_local unsigned stack_level = 0;
                ASSERT_TRUE(res);
                // Ensure receivers are invoked in exact order of initiation
                ASSERT_EQ(last_receiver_count + 1, count);
                last_receiver_count = count;
                if (ops.size() < COUNT) {
                    // Initiate another two operations to create a combinatorial
                    // explosion
                    using unique_ptr_type =
                        AsyncIO::connected_operation_unique_ptr_type<
                            timed_delay_sender,
                            receiver_t>;
                    auto initiate = [] {
                        unique_ptr_type p(
                            new unique_ptr_type::element_type(connect(
                                *testio,
                                timed_delay_sender(std::chrono::seconds(0)),
                                receiver_t{counter++})));
                        p->initiate();
                        ops.push_back(
                            std::unique_ptr<erased_connected_operation>(
                                p.release()));
                    };
                    if (stack_level > stack_depth) {
                        std::cout << "Stack depth reaches " << stack_level
                                  << std::endl;
                        stack_depth = stack_level;
                    }
                    EXPECT_LT(stack_level, 2);
                    stack_level++;
                    initiate();
                    initiate();
                    stack_level--;
                }
            }
        };
        receiver_t{counter++}.set_value(nullptr, success());
        testio->wait_until_done();
        EXPECT_GE(ops.size(), COUNT);
    }
}
