#include "gtest/gtest.h"

#include <monad/async/cpp_coroutine_wrappers.hpp>

#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/detail/boost_outcome_coroutine_support.hpp>
#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>
#include <monad/async/io_worker_pool.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/async/util.hpp>
#include <monad/core/assert.h>
#include <monad/core/small_prng.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <boost/lockfree/policies.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/outcome/try.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <sched.h>
#include <span>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <utility>
#include <vector>

namespace
{
    using namespace MONAD_ASYNC_NAMESPACE;
    static constexpr size_t TEST_FILE_SIZE = 1024 * 1024;
    static constexpr size_t MAX_CONCURRENCY = 4;
    static std::vector<std::byte> const testfilecontents = [] {
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
    inline monad::io::Ring make_ring()
    {
        return monad::io::Ring(MAX_CONCURRENCY, 0);
    }
    inline monad::io::Buffers make_buffers(monad::io::Ring &ring)
    {
        return monad::io::Buffers{
            ring, MAX_CONCURRENCY, MAX_CONCURRENCY, 1UL << 13};
    }
    static monad::async::storage_pool pool{
        monad::async::use_anonymous_inode_tag{}};
    static monad::io::Ring testring = make_ring();
    static monad::io::Buffers testrwbuf = make_buffers(testring);
    static auto testio = [] {
        auto ret = std::make_unique<AsyncIO>(pool, testring, testrwbuf);
        auto fd = pool.activate_chunk(monad::async::storage_pool::seq, 0)
                      ->write_fd(TEST_FILE_SIZE);
        MONAD_ASSERT(
            TEST_FILE_SIZE == ::pwrite(
                                  fd.first,
                                  testfilecontents.data(),
                                  TEST_FILE_SIZE,
                                  static_cast<off_t>(fd.second)));
        return ret;
    }();
    monad::small_prng test_rand;

    TEST(CppCoroutineWrappers, coroutine_read)
    {
        auto impl = []() -> awaitables::eager<result<std::vector<std::byte>>> {
            // This initiates the i/o reading DISK_PAGE_SIZE bytes from offset
            // 0, returning a coroutine awaitable
            auto awaitable = co_initiate(
                *testio,
                read_single_buffer_sender(
                    {0, 0}, std::span{(std::byte *)nullptr, DISK_PAGE_SIZE}));
            // You can do other stuff here, like initiate more i/o or do compute

            // When you really do need the result to progress further, suspend
            // execution until the i/o completes. The TRY operation will
            // propagate any failures out the return type of this lambda, if the
            // operation was successful `res` get the result.
            BOOST_OUTCOME_CO_TRY(
                std::span<const std::byte> bytesread, co_await awaitable);

            // Return a copy of the registered buffer with lifetime held by
            // awaitable
            co_return std::vector<std::byte>(
                bytesread.begin(), bytesread.end());
        };

        // Launch the coroutine task
        auto aw = impl();
        // Pump the loop until the coroutine completes
        while (!aw.await_ready()) {
            testio->poll_blocking(1);
        }
        // Get the result of the coroutine task
        result<std::vector<std::byte>> res = aw.await_resume();
        // The result may contain a failure
        if (!res) {
            std::cerr << "ERROR: " << res.error().message().c_str()
                      << std::endl;
            ASSERT_TRUE(res);
        }
        // If successful, did it read the right data?
        EXPECT_EQ(DISK_PAGE_SIZE, res.value().size());
        EXPECT_EQ(
            0,
            memcmp(
                res.value().data(), testfilecontents.data(), DISK_PAGE_SIZE));
    }

    TEST(CppCoroutineWrappers, coroutine_timeout)
    {
        auto impl =
            []() -> awaitables::eager<std::chrono::steady_clock::duration> {
            auto begin = std::chrono::steady_clock::now();
            auto r = co_await co_initiate(
                *testio, timed_delay_sender(std::chrono::seconds(1)));
            r.value();
            co_return std::chrono::steady_clock::now() - begin;
        };
        auto aw = impl();
        while (!aw.await_ready()) {
            testio->poll_blocking(1);
        }
        std::chrono::steady_clock::duration const res = aw.await_resume();
        EXPECT_GE(res, std::chrono::seconds(1));
    }

    TEST(CppCoroutineWrappers, resume_execution_upon)
    {
        std::atomic<AsyncIO *> other{nullptr};
        std::jthread thr([&](std::stop_token token) {
            monad::async::storage_pool pool{
                monad::async::use_anonymous_inode_tag{}};
            auto ring = make_ring();
            auto buf = make_buffers(ring);
            AsyncIO io(pool, ring, buf);
            other = &io;
            while (!token.stop_requested()) {
                io.poll_nonblocking(1);
            }
            io.wait_until_done();
        });
        while (other == nullptr) {
            std::this_thread::yield();
        }
        static ::boost::lockfree::queue<pid_t, ::boost::lockfree::capacity<4>>
            thread_ids;
        std::atomic<bool> done{false};
        auto impl = [&]() -> awaitables::eager<void> {
            const pid_t original_tid = gettid();
            MONAD_ASSERT(thread_ids.push(original_tid));
            (void)co_await co_resume_execution_upon(*other);
            const pid_t new_tid = gettid();
            MONAD_ASSERT(thread_ids.push(new_tid));
            // Can't complete on a thread different to original, it would be a
            // race.
            (void)co_await co_resume_execution_upon(*testio);
            const pid_t final_tid = gettid();
            MONAD_ASSERT(thread_ids.push(final_tid));
            done = true;
            co_return;
        };
        auto aw = impl();
        while (!done) {
            testio->poll_nonblocking(1);
        }
        thr.request_stop();
        thr.join();
        std::vector<pid_t> tids;
        while (!thread_ids.empty()) {
            pid_t v;
            if (thread_ids.pop(v)) {
                tids.push_back(v);
                std::cout << "\n   " << v;
            }
        }
        std::cout << std::endl;
        ASSERT_EQ(tids.size(), 3);
        EXPECT_EQ(tids[0], gettid());
        EXPECT_NE(tids[1], gettid());
        EXPECT_EQ(tids[2], gettid());
    }

    TEST(
        CppCoroutineWrappers,
        AsyncReadIoWorkerPool_custom_sender_workers_can_do_read_io)
    {
        AsyncReadIoWorkerPool workerpool(
            *testio, MAX_CONCURRENCY, make_ring, make_buffers);

        struct sender_t
        {
            using result_type = result<void>;

            chunk_offset_t const offset;
            struct receiver_t
            {
                chunk_offset_t const offset;
                erased_connected_operation *const original_io_state;
                void set_value(
                    erased_connected_operation *,
                    result<std::span<std::byte const>> res)
                {
                    if (!res) {
                        std::cerr << res.assume_error().message().c_str()
                                  << std::endl;
                    }
                    MONAD_ASSERT(res);
                    MONAD_ASSERT(
                        0 == memcmp(
                                 res.assume_value().data(),
                                 testfilecontents.data() + offset.offset,
                                 DISK_PAGE_SIZE));
                    original_io_state->completed(success());
                }
            };

            explicit sender_t(chunk_offset_t offset_)
                : offset(offset_)
            {
            }
            result_type
            operator()(erased_connected_operation *io_state) noexcept
            {
                auto state =
                    io_state->executor()
                        ->make_connected<read_single_buffer_sender, receiver_t>(
                            read_single_buffer_sender{
                                offset,
                                read_single_buffer_sender::buffer_type{
                                    (std::byte *)nullptr, DISK_PAGE_SIZE}},
                            receiver_t{offset, io_state});
                state->initiate();
                state.release();
                return success();
            }
        };
        static_assert(sender<sender_t>);
        static_assert(std::is_constructible_v<sender_t, chunk_offset_t>);
        using state_type = decltype(co_initiate(
            *testio,
            execute_on_worker_pool<sender_t>(
                workerpool, chunk_offset_t(0, 0))));
        std::deque<std::unique_ptr<state_type>> states;
        for (size_t n = 0; n < 100; n++) {
            chunk_offset_t const offset(
                0,
                round_down_align<DISK_PAGE_BITS>(test_rand() % TEST_FILE_SIZE));
            states.emplace_back(new state_type(co_initiate(
                *testio,
                execute_on_worker_pool<sender_t>(workerpool, offset))));
            while (states.size() >= MAX_CONCURRENCY) {
                testio->wait_until_done();
                if (states.front()->await_ready()) {
                    states.front()->await_resume().value();
                    states.pop_front();
                }
            }
        }
        while (!states.empty()) {
            testio->wait_until_done();
            if (states.front()->await_ready()) {
                states.front()->await_resume().value();
                states.pop_front();
            }
        }
    }

    TEST(
        CppCoroutineWrappers,
        AsyncReadIoWorkerPool_coroutine_workers_can_do_read_io)
    {
        AsyncReadIoWorkerPool workerpool( // NOLINT
            *testio,
            MAX_CONCURRENCY,
            make_ring,
            make_buffers);
        static ::boost::lockfree::queue<pid_t> thread_ids(100); // NOLINT

        auto task =
            [&](erased_connected_operation *io_state,
                chunk_offset_t offset) -> awaitables::eager<result<int>> {
            // I am on a different kernel thread
            MONAD_ASSERT(thread_ids.push(gettid()));
            // Initiate the read
            auto aw = co_initiate(
                *io_state->executor(),
                read_single_buffer_sender(
                    offset, std::span{(std::byte *)nullptr, DISK_PAGE_SIZE}));
            // Suspend until the read completes
            BOOST_OUTCOME_CO_TRY(
                std::span<const std::byte> bytesread, co_await aw);
            // Return the result of the byte comparison
            co_return memcmp(
                bytesread.data(),
                testfilecontents.data() + offset.offset,
                DISK_PAGE_SIZE);
        };
        using state_type = decltype(co_initiate(
            *testio,
            workerpool,
            std::bind(task, std::placeholders::_1, chunk_offset_t(0, 0)))); // NOLINT
        std::deque<std::unique_ptr<state_type>> states; // NOLINT
        for (size_t n = 0; n < 100; n++) { // NOLINT
            chunk_offset_t const offset(
                0,
                round_down_align<DISK_PAGE_BITS>(test_rand() % TEST_FILE_SIZE));
            std::unique_ptr<state_type> p(new auto(co_initiate(
                *testio,
                workerpool,
                std::bind(task, std::placeholders::_1, offset)))); // NOLINT
            states.push_back(std::move(p));
            while (states.size() >= MAX_CONCURRENCY) {
                testio->wait_until_done();
                if (states.front()->await_ready()) {
                    states.front()->await_resume().value();
                    states.pop_front();
                }
            }
        }
        while (!states.empty()) {
            testio->wait_until_done();
            if (states.front()->await_ready()) {
                states.front()->await_resume().value();
                states.pop_front();
            }
        }
    }

}
