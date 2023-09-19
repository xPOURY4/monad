#include "gtest/gtest.h"

#include "monad/async/io.hpp"

namespace
{
    TEST(AsyncIO, hardlink_fd_to)
    {
        monad::io::Ring testring(1, 0);
        monad::io::Buffers testrwbuf{testring, 1, 1, 1UL << 13};
        monad::async::AsyncIO testio(
            monad::async::use_anonymous_inode_tag{}, testring, testrwbuf);
        try {
            testio.dump_fd_to(0, "hardlink_fd_to_testname");
            EXPECT_TRUE(std::filesystem::exists("hardlink_fd_to_testname"));
        }
        catch (const std::system_error &e) {
            // If cross_device_link occurs, we are on a kernel before 5.3 which
            // doesn't suppose cross-filesystem copy_file_range()
            if (e.code() != std::errc::cross_device_link) {
                throw;
            }
        }
    }
}
