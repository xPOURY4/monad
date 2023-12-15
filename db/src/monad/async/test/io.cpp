#include "gtest/gtest.h"

#include <monad/async/concepts.hpp>
#include <monad/async/config.hpp>
#include <monad/async/connected_operation.hpp>
#include <monad/async/erased_connected_operation.hpp>
#include <monad/async/io.hpp>
#include <monad/async/io_senders.hpp>
#include <monad/async/storage_pool.hpp>
#include <monad/core/assert.h>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>
#include <monad/test/gtest_signal_stacktrace_printer.hpp> // NOLINT

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <memory>
#include <ostream>
#include <system_error>
#include <utility>
#include <vector>

#include <unistd.h>

namespace
{
    TEST(AsyncIO, hardlink_fd_to)
    {
        monad::async::storage_pool pool(
            monad::async::use_anonymous_inode_tag{});
        {
            auto chunk = pool.activate_chunk(pool.seq, 0);
            auto fd = chunk->write_fd(1);
            char c = 5;
            MONAD_ASSERT(
                -1 != ::pwrite(fd.first, &c, 1, static_cast<off_t>(fd.second)));
        }
        monad::io::Ring testring(1, 0);
        monad::io::Buffers testrwbuf{testring, 1, 1, 1UL << 13};
        monad::async::AsyncIO testio(pool, testring, testrwbuf);
        try {
            testio.dump_fd_to(0, "hardlink_fd_to_testname");
            EXPECT_TRUE(std::filesystem::exists("hardlink_fd_to_testname"));
        }
        catch (std::system_error const &e) {
            // If cross_device_link occurs, we are on a kernel before 5.3 which
            // doesn't suppose cross-filesystem copy_file_range()
            if (e.code() != std::errc::cross_device_link) {
                throw;
            }
        }
    }

    struct poll_does_not_recurse_receiver_t
    {
        static constexpr bool lifetime_managed_internally = false;

        int &count, &recursion_count, &max_recursion_count;
        std::vector<std::unique_ptr<monad::async::erased_connected_operation>>
            &states;

        inline void set_value(
            monad::async::erased_connected_operation *,
            monad::async::result<void>);
    };

    TEST(AsyncIO, poll_does_not_recurse)
    {
        int count = 1000000;
        int recursion_count = 0;
        int max_recursion_count = 0;
        monad::async::storage_pool pool(
            monad::async::use_anonymous_inode_tag{});
        monad::io::Ring testring(128, 0);
        monad::io::Buffers testrwbuf{
            testring, 1, 1, monad::async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE};
        monad::async::AsyncIO testio(pool, testring, testrwbuf);
        std::vector<std::unique_ptr<monad::async::erased_connected_operation>>
            states;
        states.reserve(size_t(count));
        for (size_t n = 0; n < 1000; n++) {
            std::unique_ptr<monad::async::erased_connected_operation> state(
                new auto( // NOLINT
                    monad::async::connect(
                        testio,
                        monad::async::timed_delay_sender(
                            std::chrono::seconds(0)),
                        poll_does_not_recurse_receiver_t{
                            count,
                            recursion_count,
                            max_recursion_count,
                            states})));
            state->initiate();
            states.push_back(std::move(state));
        }
        testio.wait_until_done();
        std::cout << "At worst " << max_recursion_count
                  << " recursions on stack occurred." << std::endl;
        EXPECT_LT(max_recursion_count, 2);
    }

    inline void poll_does_not_recurse_receiver_t::set_value(
        monad::async::erased_connected_operation *iostate,
        monad::async::result<void> res)
    {
        MONAD_ASSERT(res);
        if (++recursion_count > max_recursion_count) {
            max_recursion_count = recursion_count;
        }
        if (--count > 0) {
            auto &io = *iostate->executor();
            std::unique_ptr<monad::async::erased_connected_operation> state(
                new auto( // NOLINT
                    monad::async::connect(
                        io,
                        monad::async::timed_delay_sender(
                            std::chrono::seconds(0)),
                        poll_does_not_recurse_receiver_t{
                            count,
                            recursion_count,
                            max_recursion_count,
                            states})));
            state->initiate();
            states.push_back(std::move(state));
            io.poll_nonblocking_if_not_within_completions(1);
        }
        --recursion_count;
    }

    TEST(AsyncIO, buffer_exhaustion_does_not_cause_death)
    {
        monad::async::storage_pool pool(
            monad::async::use_anonymous_inode_tag{});
        monad::io::Ring testring(128, 0);
        monad::io::Buffers testrwbuf{
            testring,
            1,
            1,
            monad::async::AsyncIO::MONAD_IO_BUFFERS_READ_SIZE,
            monad::async::AsyncIO::MONAD_IO_BUFFERS_WRITE_SIZE};
        monad::async::AsyncIO testio(pool, testring, testrwbuf);

        struct empty_receiver
        {
            enum
            {
                lifetime_managed_internally = true
            };

            void set_value(
                monad::async::erased_connected_operation *,
                monad::async::read_single_buffer_sender::result_type r)
            {
                MONAD_ASSERT(r);
            }
        };

        for (size_t n = 0; n < 10; n++) {
            auto state(testio.make_connected(
                monad::async::write_single_buffer_sender(
                    {0, 0},
                    {(std::byte *)nullptr, monad::async::DISK_PAGE_SIZE}),
                empty_receiver{}));
            // Exactly the same test as the death test, except for this line
            state->initiate();
            state.release();
        }
        testio.wait_until_done();
    }
}
