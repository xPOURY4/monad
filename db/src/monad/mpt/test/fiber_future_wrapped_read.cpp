#include "gtest/gtest.h"

#include "monad/async/boost_fiber_wrappers.hpp"

#include "monad/core/small_prng.hpp"

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
            TEST_FILE_SIZE ==
            ::pwrite(
                fd.first, testfilecontents.data(), TEST_FILE_SIZE, fd.second));
        return ret;
    }();
    monad::small_prng test_rand;

    TEST(FiberFutureWrappedFind, single_thread_fibers_read)
    {
        struct receiver_t
        {
            ::boost::fibers::promise<std::span<std::byte const>> promise;
            chunk_offset_t offset;

            enum : bool
            {
                lifetime_managed_internally = false // we manage state lifetime
            };

            bool done{false};

            receiver_t(
                ::boost::fibers::promise<std::span<std::byte const>> &&p,
                chunk_offset_t const offset_)
                : promise(std::move(p))
                , offset(offset_)
            {
            }

            void set_value(
                erased_connected_operation *,
                result<std::span<const std::byte>> res)
            {
                ASSERT_TRUE(res);
                std::span<const std::byte> buffer =
                    std::move(res).assume_value();

                EXPECT_EQ(buffer.front(), testfilecontents[offset.offset]);

                promise.set_value(std::move(buffer));
                done = true;
            }
        };

        // Two diff implementations,
        // impl_sender: request read thru an io sender
        auto impl_sender = []() -> result<std::vector<std::byte>> {
            // This initiates the i/o reading DISK_PAGE_SIZE bytes from a
            // randomized offset
            using promise_result_t = std::span<std::byte const>;

            chunk_offset_t offset(
                0,
                round_down_align<DISK_PAGE_BITS>(
                    test_rand() % (TEST_FILE_SIZE - DISK_PAGE_SIZE)));
            auto sender = read_single_buffer_sender(
                offset, std::span{(std::byte *)nullptr, DISK_PAGE_SIZE});
            ::boost::fibers::promise<promise_result_t> promise;
            auto fut = promise.get_future();
            auto iostate = testio->make_connected(
                std::move(sender), receiver_t{std::move(promise), offset});
            iostate->initiate();
            std::cout << "sender..." << std::endl;

            auto bytesread = fut.get();
            // Return a copy of the registered buffer with lifetime held by fut
            return std::vector<std::byte>(bytesread.begin(), bytesread.end());
        };
        // impl_fiber_wrapper_sender: request read thru fiber wrapped io sender
        auto impl_fiber_wrapper_sender =
            []() -> result<std::vector<std::byte>> {
            // This initiates the i/o reading DISK_PAGE_SIZE bytes from offset
            // 0, returning a boost fiber future like object
            auto fut = boost_fibers::read_single_buffer(
                *testio,
                chunk_offset_t{0, 0},
                std::span{(std::byte *)nullptr, DISK_PAGE_SIZE});
            std::cout << "fiber wrapped sender..." << std::endl;
            // You can do other stuff here, like initiate more i/o or do compute

            // When you really do need the result to progress further, suspend
            // execution until the i/o completes. The TRY operation will
            // propagate any failures out the return type of this lambda, if the
            // operation was successful `res` get the result.
            BOOST_OUTCOME_TRY(std::span<const std::byte> bytesread, fut.get());

            EXPECT_EQ(DISK_PAGE_SIZE, bytesread.size());
            EXPECT_EQ(
                0,
                memcmp(
                    bytesread.data(), testfilecontents.data(), DISK_PAGE_SIZE));
            // Return a copy of the registered buffer with lifetime held by fut
            return std::vector<std::byte>(bytesread.begin(), bytesread.end());
        };

        // Launch the fiber task
        std::vector<::boost::fibers::future<result<std::vector<std::byte>>>>
            futures;
        int n_each = MAX_CONCURRENCY / 2;
        futures.reserve(MAX_CONCURRENCY);
        for (int i = 0; i < n_each; ++i) {
            futures.emplace_back(::boost::fibers::async(impl_sender));
            futures.emplace_back(
                ::boost::fibers::async(impl_fiber_wrapper_sender));
        }

        // Pump the loop until the fiber completes
        for (auto &fut : futures) {
            while (::boost::fibers::future_status::ready !=
                   fut.wait_for(std::chrono::seconds(0))) {
                testio->poll_nonblocking(1);
            }
            auto res = fut.get();
        }
    }
}