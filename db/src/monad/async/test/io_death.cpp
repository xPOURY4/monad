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

// DO NOT include gtest_signal_stacktrace_printer.hpp here, it interferes with
// Google Test's death test handling

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
    TEST(AsyncIODeathTest, buffer_exhaustion_causes_death)
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
            auto make = [&] {
                auto state(testio.make_connected(
                    monad::async::write_single_buffer_sender(
                        {0, 0},
                        {(std::byte *)nullptr, monad::async::DISK_PAGE_SIZE}),
                    empty_receiver{}));
                // Exactly the same test as the non-death test, except for this
                // line
                // state->initiate();
                state.release();
            };
            if (n > 0) {
                EXPECT_EXIT(make(), ::testing::KilledBySignal(SIGABRT), ".*");
            }
            else {
                make();
            }
        }
    }
}
