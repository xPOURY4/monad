#include <gtest/gtest.h>

#include <monad/core/running_on_ci.hpp>
#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <iostream>

TEST(HugeMem, works)
{
    using namespace MONAD_NAMESPACE;
    std::cerr << "running_on_ci = " << running_on_ci() << std::endl;
    io::Ring ring(128, 0);
    io::Buffers buffers(ring, 8, 8);
}