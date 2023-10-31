#include "gtest/gtest.h"

#include "monad/async/io.hpp"

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
            ::pwrite(fd.first, &c, 1, static_cast<off_t>(fd.second));
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
}
