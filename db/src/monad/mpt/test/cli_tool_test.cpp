#include "gtest/gtest.h"

#include "../cli_tool_impl.hpp"

#include <sstream>

#include <unistd.h>

TEST(cli_tool, no_args_prints_fatal_and_help)
{
    std::stringstream cout, cerr;
    std::string_view args[] = {"monad_mpt"};
    int retcode = main_impl(cout, cerr, args);
    ASSERT_EQ(retcode, 1);
    EXPECT_TRUE(cerr.str().starts_with("FATAL:"));
    EXPECT_NE(std::string::npos, cerr.str().find("Options:"));
}

TEST(cli_tool, help_prints_help)
{
    std::stringstream cout, cerr;
    std::string_view args[] = {"monad_mpt", "--help"};
    int retcode = main_impl(cout, cerr, args);
    ASSERT_EQ(retcode, 0);
    EXPECT_NE(std::string::npos, cout.str().find("Options:"));
}

TEST(cli_tool, create)
{
    char temppath[] = "cli_tool_test_XXXXXX";
    if (-1 == mkstemp(temppath)) {
        abort();
    }
    if (-1 == truncate(temppath, 2ULL * 1024 * 1024 * 1024)) {
        abort();
    }
    std::cout << "temp file being used: " << temppath << std::endl;
    std::stringstream cout, cerr;
    std::string_view args[] = {"monad_mpt", "--storage", temppath, "--create"};
    int retcode = main_impl(cout, cerr, args);
    unlink(temppath);
    ASSERT_EQ(retcode, 0);
    EXPECT_NE(
        std::string::npos,
        cout.str().find("1 chunks with capacity 256 Mb used 0 bytes"));
}
