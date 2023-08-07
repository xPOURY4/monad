#include <gtest/gtest.h>

#include <monad/io/buffers.hpp>
#include <monad/io/ring.hpp>

#include <iostream>

TEST(HugeMem, works)
{
    using namespace MONAD_NAMESPACE;
    io::Ring ring(128, 0);
    io::Buffers buffers(ring, 8, 8);
}
