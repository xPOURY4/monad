#include "gtest/gtest.h"

#include "monad/async/cpp_coroutine_wrappers.hpp"

#include "monad/core/small_prng.hpp"

#include <boost/lockfree/queue.hpp>

#include <thread>

#include "boost_outcome_coroutine_support.hpp"

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
    static monad::io::Ring testring = make_ring();
    static monad::io::Buffers testrwbuf = make_buffers(testring);
    static auto testio = [] {
        auto ret = std::make_unique<AsyncIO>(
            use_anonymous_inode_tag{}, testring, testrwbuf);
        MONAD_ASSERT(
            TEST_FILE_SIZE ==
            ::write(ret->get_rd_fd(), testfilecontents.data(), TEST_FILE_SIZE));
        return ret;
    }();
    // monad::small_prng test_rand;

    TEST(CppCoroutineWrappers, coroutine_read)
    {
        auto impl = []() -> awaitables::eager<result<std::vector<std::byte>>> {
            // This initiates the i/o reading DISK_PAGE_SIZE bytes from offset
            // 0, returning a coroutine awaitable
            auto awaitable = co_initiate(
                *testio,
                read_single_buffer_sender(
                    0, std::span{(std::byte *)nullptr, DISK_PAGE_SIZE}));
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
        std::chrono::steady_clock::duration res = aw.await_resume();
        EXPECT_GE(res, std::chrono::seconds(1));
    }

    TEST(CppCoroutineWrappers, resume_execution_upon)
    {
        std::atomic<AsyncIO *> other{nullptr};
        std::jthread thr([&](std::stop_token token) {
            auto ring = make_ring();
            auto buf = make_buffers(ring);
            AsyncIO io(use_anonymous_inode_tag{}, ring, buf);
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
}
